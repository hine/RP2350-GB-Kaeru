#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/clocks.h"
#include "hardware/structs/bus_ctrl.h"

#include "boards/waveshare_touch_amoled_1_8/board_config.h"
#include "drivers/display/amoled_1in8.h"
#include "drivers/audio/audio_i2s.h"
#include "drivers/audio/es8311.h"
#include "drivers/input/ft3168_touch.h"
#include "storage/flash_meta.h"
#include "storage/rom_flash.h"
#include "storage/save_flash.h"
#include "storage/save_sram.h"
#include "storage/save_state.h"
#include "emu/gb/gb_core.h"
#include "drivers/storage/storage_sd.h"

// ── 表示レイアウト ────────────────────────────────────────────────────────────
// AMOLED: 368×448
//
//  y=  0.. 15  STATUS_H  = 16px  ステータスバー（メッセージ + アイコン）
//  y= 16.. 87  SYS_BTN_H = 72px  システムボタン（MENU/SAVE/LOAD/SLOT）
//  y= 88..375  GB_H2     =288px  GB ゲーム画面（160×144 → 2×スケール）
//  y=376..447  GAME_BTN_H= 72px  ゲームボタン（SELECT/START/B/A）

#define STATUS_H     16
#define SYS_BTN_Y    STATUS_H
#define SYS_BTN_H    72
#define GB_SCALE     2
#define GB_W2        (GB_SCREEN_W * GB_SCALE)              // 320
#define GB_H2        (GB_SCREEN_H * GB_SCALE)              // 288
#define GB_X_OFF     ((AMOLED_WIDTH  - GB_W2) / 2)         // 24
#define GB_Y_OFF     (SYS_BTN_Y + SYS_BTN_H)              // 88
#define GAME_BTN_Y   (GB_Y_OFF + GB_H2)                    // 376
#define GAME_BTN_H   72
#define BTN_COL_W    (AMOLED_WIDTH / 4)                    // 92

// フローティング D-Pad デッドゾーン（ピクセル）
#define DPAD_DEAD    20
// D-Pad オーバーレイ矩形のハーフサイズ
#define DPAD_BOX     50
#define DPAD_REF_X   (AMOLED_WIDTH / 2)               // 184: ゲームエリア中央 X
#define DPAD_REF_Y   ((GB_Y_OFF + GAME_BTN_Y) / 2)    // 232: ゲームエリア中央 Y

// ── UI カラー ─────────────────────────────────────────────────────────────────
#define COL_STATUS_BG   AMOLED_COLOR(0x1082)   // ステータスバー: 暗いグレー
#define COL_WHITE       AMOLED_COLOR(0xFFFF)
#define COL_SYS_MENU    AMOLED_COLOR(0x4208)   // MENU: ダークグレー
#define COL_SYS_SAVE    AMOLED_COLOR(0x0229)   // SAVE: ダークブルー
#define COL_SYS_LOAD    AMOLED_COLOR(0x0344)   // LOAD: ダークグリーン
#define COL_SYS_SLOT    AMOLED_COLOR(0x3010)   // SLOT: ダークパープル
#define COL_BTN_SEL     AMOLED_COLOR(0x3180)   // SELECT: 暗めの黄
#define COL_BTN_STA     AMOLED_COLOR(0x0480)   // START:  暗めの緑
#define COL_BTN_B       AMOLED_COLOR(0x000C)   // B: ダークブルー
#define COL_BTN_A       AMOLED_COLOR(0x6000)   // A: ダークレッド
#define COL_DIVIDER     AMOLED_COLOR(0x2945)   // セパレータ
#define COL_DPAD_BOX    AMOLED_COLOR(0x8410)   // D-Pad 枠: グレー

// ストレージアイコン色
#define COL_ICON_READ   AMOLED_COLOR(0xFFFF)   // 白: 読み込み中
#define COL_ICON_SD     AMOLED_COLOR(0x041F)   // 青: SD 書き込み
#define COL_ICON_FLASH  AMOLED_COLOR(0xFFE0)   // 黄: Flash 書き込み

// ── パレットテーブル ──────────────────────────────────────────────────────────
#define N_PALETTES 4
static const uint16_t s_palettes[N_PALETTES][4] = {
    // 0: DMG Green
    { AMOLED_COLOR(0x9DE1), AMOLED_COLOR(0x8D61), AMOLED_COLOR(0x3306), AMOLED_COLOR(0x09C1) },
    // 1: Greyscale
    { AMOLED_COLOR(0xFFFF), AMOLED_COLOR(0xAD55), AMOLED_COLOR(0x52AA), AMOLED_COLOR(0x0000) },
    // 2: Warm (orange-amber)
    { AMOLED_COLOR(0xFFF0), AMOLED_COLOR(0xF4A0), AMOLED_COLOR(0x8240), AMOLED_COLOR(0x2000) },
    // 3: Cool (blue)
    { AMOLED_COLOR(0xCFFF), AMOLED_COLOR(0x7BDF), AMOLED_COLOR(0x0A5F), AMOLED_COLOR(0x001F) },
};
static const char *s_palette_names[N_PALETTES] = { "DMG", "Grey", "Warm", "Cool" };
static const uint16_t *s_pal = s_palettes[0];  // render_frame が参照するパレット

// ── 5×7 ビットマップフォント（Adafruit GFX 形式） ─────────────────────────────
// 各文字 5 バイト（列方向）: bit0=最上行, bit6=最下行（7行）
// 文字 0x20(space)〜0x7E(~), 95 文字分
static const uint8_t s_font5x7[95][5] = {
    {0x00,0x00,0x00,0x00,0x00}, // 0x20 ' '
    {0x00,0x00,0x5F,0x00,0x00}, // 0x21 '!'
    {0x00,0x07,0x00,0x07,0x00}, // 0x22 '"'
    {0x14,0x7F,0x14,0x7F,0x14}, // 0x23 '#'
    {0x24,0x2A,0x7F,0x2A,0x12}, // 0x24 '$'
    {0x23,0x13,0x08,0x64,0x62}, // 0x25 '%'
    {0x36,0x49,0x55,0x22,0x50}, // 0x26 '&'
    {0x00,0x05,0x03,0x00,0x00}, // 0x27 '\''
    {0x00,0x1C,0x22,0x41,0x00}, // 0x28 '('
    {0x00,0x41,0x22,0x1C,0x00}, // 0x29 ')'
    {0x08,0x2A,0x1C,0x2A,0x08}, // 0x2A '*'
    {0x08,0x08,0x3E,0x08,0x08}, // 0x2B '+'
    {0x00,0x50,0x30,0x00,0x00}, // 0x2C ','
    {0x08,0x08,0x08,0x08,0x08}, // 0x2D '-'
    {0x00,0x60,0x60,0x00,0x00}, // 0x2E '.'
    {0x20,0x10,0x08,0x04,0x02}, // 0x2F '/'
    {0x3E,0x51,0x49,0x45,0x3E}, // 0x30 '0'
    {0x00,0x42,0x7F,0x40,0x00}, // 0x31 '1'
    {0x42,0x61,0x51,0x49,0x46}, // 0x32 '2'
    {0x21,0x41,0x45,0x4B,0x31}, // 0x33 '3'
    {0x18,0x14,0x12,0x7F,0x10}, // 0x34 '4'
    {0x27,0x45,0x45,0x45,0x39}, // 0x35 '5'
    {0x3C,0x4A,0x49,0x49,0x30}, // 0x36 '6'
    {0x01,0x71,0x09,0x05,0x03}, // 0x37 '7'
    {0x36,0x49,0x49,0x49,0x36}, // 0x38 '8'
    {0x06,0x49,0x49,0x29,0x1E}, // 0x39 '9'
    {0x00,0x36,0x36,0x00,0x00}, // 0x3A ':'
    {0x00,0x56,0x36,0x00,0x00}, // 0x3B ';'
    {0x00,0x08,0x14,0x22,0x41}, // 0x3C '<'
    {0x14,0x14,0x14,0x14,0x14}, // 0x3D '='
    {0x41,0x22,0x14,0x08,0x00}, // 0x3E '>'
    {0x02,0x01,0x51,0x09,0x06}, // 0x3F '?'
    {0x32,0x49,0x79,0x41,0x3E}, // 0x40 '@'
    {0x7E,0x11,0x11,0x11,0x7E}, // 0x41 'A'
    {0x7F,0x49,0x49,0x49,0x36}, // 0x42 'B'
    {0x3E,0x41,0x41,0x41,0x22}, // 0x43 'C'
    {0x7F,0x41,0x41,0x22,0x1C}, // 0x44 'D'
    {0x7F,0x49,0x49,0x49,0x41}, // 0x45 'E'
    {0x7F,0x09,0x09,0x01,0x01}, // 0x46 'F'
    {0x3E,0x41,0x41,0x51,0x32}, // 0x47 'G'
    {0x7F,0x08,0x08,0x08,0x7F}, // 0x48 'H'
    {0x00,0x41,0x7F,0x41,0x00}, // 0x49 'I'
    {0x20,0x40,0x41,0x3F,0x01}, // 0x4A 'J'
    {0x7F,0x08,0x14,0x22,0x41}, // 0x4B 'K'
    {0x7F,0x40,0x40,0x40,0x40}, // 0x4C 'L'
    {0x7F,0x02,0x04,0x02,0x7F}, // 0x4D 'M'
    {0x7F,0x04,0x08,0x10,0x7F}, // 0x4E 'N'
    {0x3E,0x41,0x41,0x41,0x3E}, // 0x4F 'O'
    {0x7F,0x09,0x09,0x09,0x06}, // 0x50 'P'
    {0x3E,0x41,0x51,0x21,0x5E}, // 0x51 'Q'
    {0x7F,0x09,0x19,0x29,0x46}, // 0x52 'R'
    {0x46,0x49,0x49,0x49,0x31}, // 0x53 'S'
    {0x01,0x01,0x7F,0x01,0x01}, // 0x54 'T'
    {0x3F,0x40,0x40,0x40,0x3F}, // 0x55 'U'
    {0x1F,0x20,0x40,0x20,0x1F}, // 0x56 'V'
    {0x7F,0x20,0x18,0x20,0x7F}, // 0x57 'W'
    {0x63,0x14,0x08,0x14,0x63}, // 0x58 'X'
    {0x03,0x04,0x78,0x04,0x03}, // 0x59 'Y'
    {0x61,0x51,0x49,0x45,0x43}, // 0x5A 'Z'
    {0x00,0x7F,0x41,0x41,0x00}, // 0x5B '['
    {0x02,0x04,0x08,0x10,0x20}, // 0x5C '\'
    {0x00,0x41,0x41,0x7F,0x00}, // 0x5D ']'
    {0x04,0x02,0x01,0x02,0x04}, // 0x5E '^'
    {0x40,0x40,0x40,0x40,0x40}, // 0x5F '_'
    {0x00,0x01,0x02,0x04,0x00}, // 0x60 '`'
    {0x20,0x54,0x54,0x54,0x78}, // 0x61 'a'
    {0x7F,0x48,0x44,0x44,0x38}, // 0x62 'b'
    {0x38,0x44,0x44,0x44,0x20}, // 0x63 'c'
    {0x38,0x44,0x44,0x48,0x7F}, // 0x64 'd'
    {0x38,0x54,0x54,0x54,0x18}, // 0x65 'e'
    {0x08,0x7E,0x09,0x01,0x02}, // 0x66 'f'
    {0x08,0x54,0x54,0x54,0x3C}, // 0x67 'g'
    {0x7F,0x08,0x04,0x04,0x78}, // 0x68 'h'
    {0x00,0x44,0x7D,0x40,0x00}, // 0x69 'i'
    {0x20,0x40,0x44,0x3D,0x00}, // 0x6A 'j'
    {0x7F,0x10,0x28,0x44,0x00}, // 0x6B 'k'
    {0x00,0x41,0x7F,0x40,0x00}, // 0x6C 'l'
    {0x7C,0x04,0x18,0x04,0x78}, // 0x6D 'm'
    {0x7C,0x08,0x04,0x04,0x78}, // 0x6E 'n'
    {0x38,0x44,0x44,0x44,0x38}, // 0x6F 'o'
    {0x7C,0x14,0x14,0x14,0x08}, // 0x70 'p'
    {0x08,0x14,0x14,0x18,0x7C}, // 0x71 'q'
    {0x7C,0x08,0x04,0x04,0x08}, // 0x72 'r'
    {0x48,0x54,0x54,0x54,0x20}, // 0x73 's'
    {0x04,0x3F,0x44,0x40,0x20}, // 0x74 't'
    {0x3C,0x40,0x40,0x20,0x7C}, // 0x75 'u'
    {0x1C,0x20,0x40,0x20,0x1C}, // 0x76 'v'
    {0x3C,0x40,0x30,0x40,0x3C}, // 0x77 'w'
    {0x44,0x28,0x10,0x28,0x44}, // 0x78 'x'
    {0x0C,0x50,0x50,0x50,0x3C}, // 0x79 'y'
    {0x44,0x64,0x54,0x4C,0x44}, // 0x7A 'z'
    {0x00,0x08,0x36,0x41,0x00}, // 0x7B '{'
    {0x00,0x00,0x7F,0x00,0x00}, // 0x7C '|'
    {0x00,0x41,0x36,0x08,0x00}, // 0x7D '}'
    {0x08,0x04,0x08,0x10,0x08}, // 0x7E '~'
};

// ── フレームバッファ ─────────────────────────────────────────────────────────
static uint8_t  s_gb[2][GB_SCREEN_H][GB_SCREEN_W];
static uint16_t s_fb[AMOLED_HEIGHT][AMOLED_WIDTH];

// ── Core 間共有 ──────────────────────────────────────────────────────────────
static volatile int      g_lcd_idx    = 0;
static volatile bool     g_lcd_busy   = false;
static volatile bool     g_frame_tick = false;
static volatile bool     g_touch_flag = false;

// フローティング D-Pad（Core 0 書き / Core 1 読み）
static volatile bool     g_dpad_active = false;
static volatile int16_t  g_dpad_ox = 0, g_dpad_oy = 0;
static volatile int16_t  g_dpad_cx = 0, g_dpad_cy = 0;

// ステータスバー（Core 0 書き / Core 1 読み）
static volatile char     g_status_msg[32];
static volatile int      g_status_ttl = 0;
static volatile uint16_t g_storage_icon_color = 0;

static int g_gb_write = 0;

// ── メニュー / ポーズ ─────────────────────────────────────────────────────────
static bool                g_menu_active = false;
static repeating_timer_t   g_frame_timer;

// パレット選択
static int  g_palette_idx = 0;

// 音量ステップ: 0=Off, 1=20%, 2=40%, 3=60%, 4=80%, 5=100%
static int  g_vol_step    = 3;  // 起動時 60%
static const int  s_vol_values[6] = { 0, 20, 40, 60, 80, 100 };
static const char *s_vol_labels[6] = { "Off", "20%", "40%", "60%", "80%", "100%" };

static void apply_volume(void) {
    if (g_vol_step == 0) {
        es8311_mute(BOARD_I2C, true);
    } else {
        es8311_mute(BOARD_I2C, false);
        es8311_set_volume(BOARD_I2C, s_vol_values[g_vol_step]);
    }
}

// ── セーブ関連 ───────────────────────────────────────────────────────────────
static int  g_state_slot           = 0;
static int  g_sram_dirty_countdown = 0;

// ── タッチ入力状態（Core 0 だけが読み書き） ─────────────────────────────────
static uint8_t s_btn_joy    = 0xFF;  // ゲームボタン bits
// FT3168 が ev=1(離し)を送らないため、IRQ が N フレーム来なければ「指離れ」と判定する
static int     s_touch_age  = 0;     // 最後の IRQ から経過フレーム数
#define TOUCH_LIFT_FRAMES 4          // この値以上なら指が離れたとみなす
// ゾーン進入追跡
static int     s_touch_zone        = -1;    // -1=未タッチ, 0=status, 1=sys, 2=game, 3=btn
// D-Pad セッションフラグ: ゲームエリアに一度でも入ったセッションでは
// 他のゾーン（sys_btn / game_btn）への誤入力を発火させない
static bool    s_touched_game_area = false;

// ── 音声 SPSC リングバッファ ─────────────────────────────────────────────────
// GB_AUDIO_SAMPLES / GB_AUDIO_SAMPLES_TOTAL は gb_core.h で定義済み

#define AFIFO_SIZE  4096u
static uint32_t          s_afifo[AFIFO_SIZE];
static volatile uint16_t s_afifo_wr = 0;
static volatile uint16_t s_afifo_rd = 0;

static void __not_in_flash_func(audio_fill)(uint32_t *dst, int n) {
    uint16_t rd  = s_afifo_rd;
    int      got = (int)(uint16_t)(s_afifo_wr - rd);
    if (got > n) got = n;
    for (int i = 0; i < got; i++)
        dst[i] = s_afifo[(rd++) & (AFIFO_SIZE - 1u)];
    for (int i = got; i < n; i++)
        dst[i] = 0;
    __dmb();
    s_afifo_rd = rd;
}

#define BRES_ADDEND  785
#define BRES_THRESH  1024
static int s_bres_acc = 0;

static void afifo_push_apu(const int16_t *src, int n_pairs, int extra_silence) {
    uint16_t wr    = s_afifo_wr;
    uint16_t rd    = s_afifo_rd;
    int      space = (int)(uint16_t)((uint16_t)(AFIFO_SIZE - 1u) - (wr - rd));
    int      push  = n_pairs + extra_silence;
    if (push > space) push = space;
    for (int i = 0; i < push && i < n_pairs; i++) {
        int16_t l = src[2 * i];
        int16_t r = src[2 * i + 1];
        s_afifo[(wr++) & (AFIFO_SIZE - 1u)] =
            ((uint32_t)(uint16_t)l << 16) | (uint16_t)r;
    }
    for (int i = n_pairs; i < push; i++)
        s_afifo[(wr++) & (AFIFO_SIZE - 1u)] = 0;
    __dmb();
    s_afifo_wr = wr;
}

static int16_t s_apu_buf[GB_AUDIO_SAMPLES_TOTAL];

// ── 汎用描画プリミティブ ─────────────────────────────────────────────────────
static void fb_fill(int x0, int y0, int x1, int y1, uint16_t c) {
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > AMOLED_WIDTH)  x1 = AMOLED_WIDTH;
    if (y1 > AMOLED_HEIGHT) y1 = AMOLED_HEIGHT;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            s_fb[y][x] = c;
}

// 矩形アウトライン（1px 太さ）
static void fb_rect(int x0, int y0, int x1, int y1, uint16_t c) {
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > AMOLED_WIDTH)  x1 = AMOLED_WIDTH;
    if (y1 > AMOLED_HEIGHT) y1 = AMOLED_HEIGHT;
    for (int x = x0; x < x1; x++) {
        if (y0 >= 0 && y0 < AMOLED_HEIGHT) s_fb[y0][x] = c;
        if (y1-1 >= 0 && y1-1 < AMOLED_HEIGHT) s_fb[y1-1][x] = c;
    }
    for (int y = y0+1; y < y1-1; y++) {
        if (x0 >= 0 && x0 < AMOLED_WIDTH) s_fb[y][x0] = c;
        if (x1-1 >= 0 && x1-1 < AMOLED_WIDTH) s_fb[y][x1-1] = c;
    }
}

// ── フォント描画 ─────────────────────────────────────────────────────────────
// 1文字描画（scale: 1=5×7px, 2=10×14px）
static void fb_draw_char(int x, int y, unsigned char ch, uint16_t fg, uint16_t bg, int sc) {
    if (ch < 0x20 || ch > 0x7E) ch = '?';
    const uint8_t *bm = s_font5x7[ch - 0x20];
    for (int col = 0; col < 5; col++) {
        uint8_t col_data = bm[col];
        for (int row = 0; row < 7; row++) {
            uint16_t px = (col_data & (1 << row)) ? fg : bg;
            int px_x = x + col * sc;
            int px_y = y + row * sc;
            for (int sy = 0; sy < sc; sy++)
                for (int sx = 0; sx < sc; sx++) {
                    int fx = px_x + sx, fy = px_y + sy;
                    if (fx >= 0 && fx < AMOLED_WIDTH && fy >= 0 && fy < AMOLED_HEIGHT)
                        s_fb[fy][fx] = px;
                }
        }
    }
}

// 文字列描画（改行なし）。戻り値: 描画した幅 px
static int fb_draw_text(int x, int y, const char *str, uint16_t fg, uint16_t bg, int sc) {
    int cx = x;
    while (*str) {
        fb_draw_char(cx, y, (unsigned char)*str, fg, bg, sc);
        cx += 6 * sc;  // 5px + 1px gap
        str++;
    }
    return cx - x;
}

// 文字列を幅 w のエリア中央に描画
static void fb_draw_text_center(int x, int y, int w, const char *str,
                                 uint16_t fg, uint16_t bg, int sc) {
    int tw = (int)strlen(str) * 6 * sc;
    fb_draw_text(x + (w - tw) / 2, y, str, fg, bg, sc);
}

// ── ステータスバー描画（Core 1 から毎フレーム呼ぶ） ─────────────────────────
static void draw_status_bar(void) {
    fb_fill(0, 0, AMOLED_WIDTH, STATUS_H, COL_STATUS_BG);

    // メッセージテキスト（左寄せ、scale=1: 5×7px、y=4 で縦中央）
    if (g_status_ttl > 0) {
        char msg[32];
        memcpy(msg, (const void *)g_status_msg, sizeof(msg));
        fb_draw_text(4, (STATUS_H - 7) / 2, msg, COL_WHITE, COL_STATUS_BG, 1);
    }

    // ストレージアイコン（右端 10×10px）
    uint16_t ic = g_storage_icon_color;
    if (ic != 0) {
        fb_fill(AMOLED_WIDTH - 13, 3, AMOLED_WIDTH - 3, STATUS_H - 3, ic);
    }
}

// ── ボタン 1 個を描画するヘルパー ────────────────────────────────────────────
static void draw_btn(int col_idx, int y_top, int h, const char *label, uint16_t bg) {
    int x0 = col_idx * BTN_COL_W;
    int x1 = x0 + BTN_COL_W;
    fb_fill(x0, y_top, x1, y_top + h, bg);
    // セパレータ（右端 1px、最終列除く）
    if (col_idx < 3)
        fb_fill(x1 - 1, y_top, x1, y_top + h, COL_DIVIDER);
    // テキスト縦中央: scale=2 文字高 14px
    int ty = y_top + (h - 14) / 2;
    fb_draw_text_center(x0, ty, BTN_COL_W, label, COL_WHITE, bg, 2);
}

// ── メニューオーバーレイ描画（Core 1 から毎フレーム呼ぶ） ────────────────────
// 5項目: Palette / Volume / Backup SD / Restore SD / Close
#define MENU_N_ITEMS   5
#define MENU_ITEM_H    48
#define MENU_TITLE_Y   (GB_Y_OFF + 12)
#define MENU_ITEMS_Y   (GB_Y_OFF + 44)
#define MENU_BG        AMOLED_COLOR(0x1082)
#define MENU_ITEM_BG   AMOLED_COLOR(0x2104)
#define MENU_ITEM_SEL  AMOLED_COLOR(0x3186)

static int s_menu_item = -1;  // 選択中の項目(-1=なし)

static void draw_menu_overlay(void) {
    // 半暗オーバーレイ
    fb_fill(GB_X_OFF, GB_Y_OFF, GB_X_OFF + GB_W2, GAME_BTN_Y, MENU_BG);
    // タイトル
    fb_draw_text_center(GB_X_OFF, MENU_TITLE_Y, GB_W2, "- MENU -", COL_WHITE, MENU_BG, 2);
    fb_fill(GB_X_OFF, MENU_TITLE_Y + 22, GB_X_OFF + GB_W2, MENU_TITLE_Y + 23, COL_DIVIDER);

    static const char *labels[MENU_N_ITEMS] = {
        "Palette", "Volume", "Backup SD", "Restore SD", "Close"
    };
    for (int i = 0; i < MENU_N_ITEMS; i++) {
        int iy  = MENU_ITEMS_Y + i * MENU_ITEM_H;
        int iy1 = iy + MENU_ITEM_H - 2;
        uint16_t bg = (i == s_menu_item) ? MENU_ITEM_SEL : MENU_ITEM_BG;
        fb_fill(GB_X_OFF, iy, GB_X_OFF + GB_W2, iy1, bg);
        fb_fill(GB_X_OFF, iy1, GB_X_OFF + GB_W2, iy1 + 1, COL_DIVIDER);

        int ty = iy + (MENU_ITEM_H - 14) / 2;
        fb_draw_text(GB_X_OFF + 10, ty, labels[i], COL_WHITE, bg, 2);

        // 現在値を右寄せ
        char val[12] = "";
        if (i == 0) snprintf(val, sizeof(val), "%s", s_palette_names[g_palette_idx]);
        if (i == 1) snprintf(val, sizeof(val), "%s", s_vol_labels[g_vol_step]);
        if (val[0]) {
            int vw = (int)strlen(val) * 12;
            fb_draw_text(GB_X_OFF + GB_W2 - vw - 10, ty, val, COL_WHITE, bg, 2);
        }
    }
}

// ── システムボタン描画（起動時 + SLOT 更新時） ───────────────────────────────
static void draw_system_buttons(void) {
    char slot_label[8];
    snprintf(slot_label, sizeof(slot_label), "SL:%d", g_state_slot);

    draw_btn(0, SYS_BTN_Y, SYS_BTN_H, "MENU", COL_SYS_MENU);
    draw_btn(1, SYS_BTN_Y, SYS_BTN_H, "SAVE", COL_SYS_SAVE);
    draw_btn(2, SYS_BTN_Y, SYS_BTN_H, "LOAD", COL_SYS_LOAD);
    draw_btn(3, SYS_BTN_Y, SYS_BTN_H, slot_label, COL_SYS_SLOT);
}

// ── ゲームボタン描画（起動時のみ、AMOLED GRAM に保持） ───────────────────────
static void draw_game_buttons(void) {
    draw_btn(0, GAME_BTN_Y, GAME_BTN_H, "SEL",   COL_BTN_SEL);
    draw_btn(1, GAME_BTN_Y, GAME_BTN_H, "START", COL_BTN_STA);
    draw_btn(2, GAME_BTN_Y, GAME_BTN_H, "B",     COL_BTN_B);
    draw_btn(3, GAME_BTN_Y, GAME_BTN_H, "A",     COL_BTN_A);
}

// ── D-Pad オーバーレイ（Core 1 から毎フレーム呼ぶ、active 時のみ） ──────────
static void draw_dpad_overlay(void) {
    int ox = g_dpad_ox, oy = g_dpad_oy;  // タッチ開始点（原点）
    int cx = g_dpad_cx, cy = g_dpad_cy;  // 現在位置

    // 原点に十字（ここが基準）
    fb_fill(ox - DPAD_BOX, oy - 1, ox + DPAD_BOX, oy + 1, COL_DPAD_BOX);
    fb_fill(ox - 1, oy - DPAD_BOX, ox + 1, oy + DPAD_BOX, COL_DPAD_BOX);

    // デッドゾーン矩形（原点中心）
    fb_rect(ox - DPAD_DEAD, oy - DPAD_DEAD,
            ox + DPAD_DEAD, oy + DPAD_DEAD, COL_DPAD_BOX);

    // 現在のタッチ位置（白い小十字）
    fb_fill(cx - 3, cy - 1, cx + 3, cy + 1, COL_WHITE);
    fb_fill(cx - 1, cy - 3, cx + 1, cy + 3, COL_WHITE);
}

// ── セーブ / ロード操作 ──────────────────────────────────────────────────────
static void do_save_state(void) {
    g_storage_icon_color = COL_ICON_FLASH;
    multicore_lockout_start_blocking();
    save_flash_state_save(g_state_slot);
    multicore_lockout_end_blocking();
    g_storage_icon_color = 0;
    strncpy((char *)g_status_msg, "Saved", sizeof(g_status_msg));
    g_status_ttl = 120;
    printf("State saved: slot %d\n", g_state_slot);
}

static void do_load_state(void) {
    g_storage_icon_color = COL_ICON_FLASH;
    int r = save_flash_state_load(g_state_slot);
    g_storage_icon_color = 0;
    if (r == 0) {
        strncpy((char *)g_status_msg, "Loaded", sizeof(g_status_msg));
        printf("State loaded: slot %d\n", g_state_slot);
    } else {
        strncpy((char *)g_status_msg, "No data", sizeof(g_status_msg));
        printf("State load: slot %d empty\n", g_state_slot);
    }
    g_status_ttl = 120;
}

// ── タッチ入力処理 ───────────────────────────────────────────────────────────
// システムボタン（押下時に一度だけ実行）
// ── メニュー / ポーズ操作 ────────────────────────────────────────────────────
// PicoCalc キーとの対応:
//   MENU(col0) = ESC : ゲームをポーズしてメニュー表示
//   SAVE(col1) = F2  : セーブステート書き込み
//   LOAD(col2) = F3  : セーブステート読み込み
//   SLOT(col3) = F4  : スロット切り替え

static void do_menu_open(void) {
    // フレームタイマーはキャンセルしない（タイマーが止まると while(!g_frame_tick) で
    // 永久にブロックしてしまい、MENU 再タップを受け付けられなくなる）
    g_menu_active = true;
    g_dpad_active = false;
    s_btn_joy     = 0xFF;
    strncpy((char *)g_status_msg, "PAUSED", sizeof(g_status_msg));
    g_status_ttl  = 9999;
    printf("Menu: open\n");
}

static void do_menu_close(void) {
    g_menu_active = false;
    g_status_ttl  = 0;
    // 設定を Flash に保存（PicoCalc と同じ: メニューを閉じたとき）
    // audio_en フィールドを vol_step（0-5）として流用
    multicore_lockout_start_blocking();
    flash_settings_save((uint8_t)g_palette_idx, (uint8_t)g_vol_step, 80);
    multicore_lockout_end_blocking();
    printf("Menu: close, settings saved (pal=%d vol=%d)\n", g_palette_idx, g_vol_step);
}

static void handle_sys_btn(int col) {
    switch (col) {
        case 0:
            // MENU(ESC): ポーズ/再開トグル（メニュー UI は後で実装）
            if (g_menu_active) do_menu_close();
            else               do_menu_open();
            break;
        case 1: do_save_state(); break;
        case 2: do_load_state(); break;
        case 3:
            g_state_slot = (g_state_slot + 1) % SAVE_FLASH_STATE_N_SLOTS;
            draw_system_buttons();
            printf("Slot: %d\n", g_state_slot);
            break;
    }
}

// process_touch: IRQ 発火時にタッチ状態を更新する。
//
// FT3168 の実機挙動（確認済み）:
//   - 常に ev=2（接触継続）しか送らない。ev=0（押下開始）・ev=1（離し）は来ない。
//   - 「新規押下」検出: ゾーンが変わったとき (new_zone) に一度だけアクションを実行。
//   - 「指離れ」検出: IRQ が TOUCH_LIFT_FRAMES 以上来なくなったら離れたとみなす
//                     （main ループ側のタイムアウト処理に委ねる）。
static void process_menu_touch(const ft3168_data_t *td);  // forward decl

static void process_touch(const ft3168_data_t *td) {
    if (td->n_points == 0) return;

    // メニュー表示中はメニュー専用処理へ
    if (g_menu_active) {
        process_menu_touch(td);
        return;
    }

    int tx = (int)td->p[0].x;
    int ty = (int)td->p[0].y;

    // ゾーン判定
    int zone;
    if      (ty < STATUS_H)    zone = 0;  // ステータスバー
    else if (ty < GB_Y_OFF)    zone = 1;  // システムボタン
    else if (ty < GAME_BTN_Y)  zone = 2;  // ゲームエリア (D-Pad)
    else                       zone = 3;  // ゲームボタン

    bool new_zone = (zone != s_touch_zone);
    s_touch_zone = zone;

    switch (zone) {
        case 0:  // ステータスバー: 何もしない
            g_dpad_active = false;
            s_btn_joy = 0xFF;
            break;

        case 1:  // システムボタン: ゾーン初入かつ D-Pad セッションでないときのみ実行
            g_dpad_active = false;
            s_btn_joy = 0xFF;
            if (new_zone && !s_touched_game_area)
                handle_sys_btn(tx / BTN_COL_W);
            break;

        case 2:  // ゲームエリア: D-Pad（タッチ開始位置が原点）
            s_touched_game_area = true;
            s_btn_joy = 0xFF;
            if (new_zone) {
                // ゾーン初入時: 原点をタッチ開始位置にセット
                g_dpad_ox = (int16_t)tx;
                g_dpad_oy = (int16_t)ty;
            }
            g_dpad_active = true;
            g_dpad_cx = (int16_t)tx;
            g_dpad_cy = (int16_t)ty;
            break;

        case 3:  // ゲームボタン: D-Pad セッション中は無効
            g_dpad_active = false;
            s_btn_joy = 0xFF;
            if (!s_touched_game_area) {
                switch (tx / BTN_COL_W) {
                    case 0: s_btn_joy &= ~JOYPAD_SELECT; break;
                    case 1: s_btn_joy &= ~JOYPAD_START;  break;
                    case 2: s_btn_joy &= ~JOYPAD_B;      break;
                    case 3: s_btn_joy &= ~JOYPAD_A;      break;
                }
            }
            break;
    }
}

// ── メニュータッチ処理 ───────────────────────────────────────────────────────
static void process_menu_touch(const ft3168_data_t *td) {
    if (td->n_points == 0) return;
    int tx = (int)td->p[0].x;
    int ty = (int)td->p[0].y;

    // メニュー項目ゾーン（ゲームエリア内）
    if (ty < MENU_ITEMS_Y || ty >= MENU_ITEMS_Y + MENU_N_ITEMS * MENU_ITEM_H) return;
    int item = (ty - MENU_ITEMS_Y) / MENU_ITEM_H;
    (void)tx;

    bool new_item = (item != s_menu_item);
    s_menu_item = item;
    if (!new_item) return;  // 同じ項目を保持中は発火しない

    switch (item) {
        case 0:  // Palette: サイクル
            g_palette_idx = (g_palette_idx + 1) % N_PALETTES;
            s_pal = s_palettes[g_palette_idx];
            break;
        case 1:  // Volume: サイクル
            g_vol_step = (g_vol_step + 1) % 6;
            apply_volume();
            break;
        case 2:  // Backup SD: TODO
            printf("Menu: SD backup (TODO)\n");
            break;
        case 3:  // Restore SD: TODO
            printf("Menu: SD restore (TODO)\n");
            break;
        case 4:  // Close: メニューを閉じてゲーム再開
            s_menu_item = -1;
            do_menu_close();
            break;
    }
}

// 毎フレーム呼び出し: タッチ開始位置を原点として変位で方向判定する
static uint8_t compute_dpad_joy(void) {
    if (!g_dpad_active) return 0xFF;
    uint8_t joy = 0xFF;
    int dx = (int)g_dpad_cx - (int)g_dpad_ox;
    int dy = (int)g_dpad_cy - (int)g_dpad_oy;
    if (dy < -DPAD_DEAD) joy &= ~JOYPAD_UP;
    if (dy >  DPAD_DEAD) joy &= ~JOYPAD_DOWN;
    if (dx < -DPAD_DEAD) joy &= ~JOYPAD_LEFT;
    if (dx >  DPAD_DEAD) joy &= ~JOYPAD_RIGHT;
    return joy;
}

// ── GB フレーム → フレームバッファ 2× スケール変換 ──────────────────────────
static void render_frame(const uint8_t src[GB_SCREEN_H][GB_SCREEN_W]) {
    for (int y = 0; y < GB_SCREEN_H; y++) {
        const uint8_t *row = src[y];
        int dy = GB_Y_OFF + y * 2;
        for (int x = 0; x < GB_SCREEN_W; x++) {
            uint16_t c  = s_pal[row[x] & 3];
            int      dx = GB_X_OFF + x * 2;
            s_fb[dy][dx]         = c;
            s_fb[dy][dx + 1]     = c;
            s_fb[dy + 1][dx]     = c;
            s_fb[dy + 1][dx + 1] = c;
        }
    }
}

// ── IRQ / タイマーコールバック ────────────────────────────────────────────────
static void gpio_irq_handler(uint gpio, uint32_t events) {
    if (gpio == TOUCH_INT_PIN && (events & GPIO_IRQ_EDGE_RISE))
        g_touch_flag = true;
}

static bool frame_timer_cb(repeating_timer_t *rt) {
    (void)rt;
    g_frame_tick = true;
    return true;
}

// ── Core 1: GB フレームレンダリング + AMOLED 転送 ────────────────────────────
static void core1_main(void) {
    multicore_lockout_victim_init();

    while (true) {
        if (!g_lcd_busy) continue;

        int idx = g_lcd_idx;
        render_frame(s_gb[idx]);

        // ステータスバーを s_fb に反映（テキスト + アイコン）
        draw_status_bar();

        // D-Pad オーバーレイ / メニューオーバーレイ（排他）
        if (g_menu_active)
            draw_menu_overlay();
        else if (g_dpad_active)
            draw_dpad_overlay();

        // ステータスバー + システムボタン + ゲームエリアを一括転送
        // ゲームボタン（GAME_BTN_Y〜AMOLED_HEIGHT）は AMOLED GRAM に保持
        amoled_1in8_display_window(0, 0, AMOLED_WIDTH, GAME_BTN_Y,
                                   (const uint16_t *)s_fb);
        __dmb();
        g_lcd_busy = false;
    }
}

// ── main ─────────────────────────────────────────────────────────────────────
int main(void) {
    set_sys_clock_khz(200 * 1000, true);
    clock_configure(clk_peri, 0,
        CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
        200000000, 200000000);

    stdio_init_all();
    sleep_ms(200);
    printf("\n=== AMOLED GB Kaeru ===\n");

    // I2C（FT3168 / ES8311 共有バス）
    i2c_init(BOARD_I2C, BOARD_I2C_HZ);
    gpio_set_function(BOARD_I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(BOARD_I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(BOARD_I2C_SDA);
    gpio_pull_up(BOARD_I2C_SCL);

    // ES8311 音声コーデック初期化
    {
        uint32_t mclk_hz = (uint32_t)AUDIO_I2S_SAMPLE_RATE * 192u;
        es8311_init(BOARD_I2C, AUDIO_I2S_SAMPLE_RATE, mclk_hz);
        es8311_set_volume(BOARD_I2C, 60);
        es8311_mute(BOARD_I2C, false);
        printf("ES8311: ID=0x%04X\n", es8311_read_id(BOARD_I2C));
    }

    // AMOLED ディスプレイ初期化
    amoled_1in8_init();
    amoled_1in8_set_brightness(80);

    // 初期 UI 描画（全画面転送: システムボタン + ゲームボタンを GRAM に確定）
    memset(s_fb, 0, sizeof(s_fb));
    draw_system_buttons();
    draw_game_buttons();
    amoled_1in8_display((const uint16_t *)s_fb);

    // ── ROM 準備 ─────────────────────────────────────────────────────────────
    #define ROM_PATH "0:/roms/kaeru.gb"
    bool rom_in_flash = flash_meta_rom_valid();
    bool sd_mounted   = false;

    printf("ROM: %s\n", rom_in_flash ? "Flash OK" : "SD required");

    if (!rom_in_flash) {
        sleep_ms(500);
        int fr = FR_NOT_READY;
        while (fr != FR_OK) {
            sd_unmount();
            sleep_ms(500);
            fr = sd_mount();
            printf("SD: %s\n", fr == FR_OK ? "OK" : "retrying...");
        }
        sd_mounted = true;
    } else {
        sleep_ms(300);
        if (sd_mount() == FR_OK) {
            sd_mounted = true;
            printf("SD: OK\n");
        } else {
            printf("SD: (skip)\n");
        }
    }

    if (sd_mounted) {
        int rc = rom_flash_ensure(ROM_PATH);
        if (rc == 0) {
            flash_meta_set_rom(rom_flash_ptr() + 0x0134);
            rom_in_flash = true;
        } else if (!rom_in_flash) {
            printf("ROM load from SD failed: %d\n", rc);
        }
    }

    if (!rom_in_flash) {
        const uint8_t *rom = rom_flash_ptr();
        if (rom[0x104] != 0xFF) {
            printf("ROM: raw data found, registering...\n");
            flash_meta_set_rom(rom + 0x0134);
            rom_in_flash = true;
        }
    }

    if (!rom_in_flash) {
        printf("ERROR: No ROM found.\n");
        while (true) tight_loop_contents();
    }
    printf("ROM: ready\n");

    // GB コア初期化
    int rc = gb_core_init();
    if (rc != 0) {
        printf("GB init failed: %d\n", rc);
        while (true) tight_loop_contents();
    }
    printf("GB: OK\n");

    gb_core_set_fb(s_gb[0]);
    g_gb_write = 0;
    gb_core_set_joypad(0xFF);

    // 設定ロード（PicoCalc と同じ: ROM ロード後に実施）
    {
        uint8_t pal, vol, bl;
        flash_settings_load(&pal, &vol, &bl);
        g_palette_idx = (pal < N_PALETTES) ? pal : 0;
        g_vol_step    = (vol < 6)          ? vol : 3;  // audio_en を vol_step として流用
        s_pal = s_palettes[g_palette_idx];
        // 音量適用は audio_i2s_init 後（ES8311 が有効になってから）
        printf("Settings: pal=%d vol=%d\n", g_palette_idx, g_vol_step);
    }

    // SRAM ロード
    if (gb_core_save_size() > 0) {
        int sr = save_flash_sram_load(gb_core_cart_ram_ptr(), gb_core_save_size());
        printf("SRAM: %s\n", sr == 0 ? "loaded" : "no save");
    }

    // I2S DMA 音声ドライバ起動
    audio_i2s_set_fill_cb(audio_fill);
    audio_i2s_init();
    printf("Audio: OK\n");
    apply_volume();  // Flash から読み込んだ音量を ES8311 に適用

    // POWER ボタン（GPIO18 = SYS_OUT_PIN）: BSS138 反転回路
    //   未押下: PWRON=HIGH → BSS138 ON → GPIO18=LOW
    //   押下時: PWRON=LOW  → BSS138 OFF → GPIO18=HIGH（R13 1K + 内部プルアップ）
    // → active-HIGH: gpio_get()=true のとき A 押下
    gpio_init(SYS_OUT_PIN);
    gpio_set_dir(SYS_OUT_PIN, GPIO_IN);
    gpio_pull_up(SYS_OUT_PIN);

    // FT3168 タッチ（ポイントモード）
    ft3168_init(BOARD_I2C, TOUCH_RST_PIN, FT3168_MODE_POINT);
    gpio_init(TOUCH_INT_PIN);
    gpio_pull_up(TOUCH_INT_PIN);
    gpio_set_dir(TOUCH_INT_PIN, GPIO_IN);
    gpio_set_irq_enabled_with_callback(TOUCH_INT_PIN,
        GPIO_IRQ_EDGE_RISE, true, gpio_irq_handler);

    // Core 1 起動
    multicore_launch_core1(core1_main);

    // GB フレームタイマー（59.727fps = 16743μs）; g_frame_timer はグローバル（ポーズ時に cancel）
    add_repeating_timer_us(-16743, frame_timer_cb, NULL, &g_frame_timer);

    printf("Running.\n");

    static ft3168_data_t touch = {0};

    while (true) {
        while (!g_frame_tick) tight_loop_contents();
        g_frame_tick = false;

        // タッチ読み取りと入力処理
        if (g_touch_flag) {
            g_touch_flag = false;
            s_touch_age = 0;
            ft3168_read(BOARD_I2C, &touch);
            process_touch(&touch);
        } else {
            // IRQ が来ない間カウントアップ → TOUCH_LIFT_FRAMES 到達で「指離れ」扱い
            if (s_touch_age < TOUCH_LIFT_FRAMES) {
                s_touch_age++;
            }
            if (s_touch_age >= TOUCH_LIFT_FRAMES) {
                // s_touch_zone の状態にかかわらず全タッチ状態をリセット
                // （メニュー中は s_touch_zone が -1 のままのため条件なしで判定）
                g_dpad_active       = false;
                s_btn_joy           = 0xFF;
                s_touch_zone        = -1;
                s_touched_game_area = false;
                s_menu_item         = -1;  // 次のタップで same_item も new_item=true になる
            }
        }

        // Core 1 へ描画ディスパッチ（ポーズ中も必須: メニューオーバーレイを表示するため）
        if (!g_lcd_busy) {
            g_lcd_idx = g_gb_write;
            __dmb();
            g_lcd_busy = true;
            if (!g_menu_active) {
                // ゲーム動作中のみバッファを進める
                g_gb_write ^= 1;
                gb_core_set_fb(s_gb[g_gb_write]);
            }
            // ポーズ中は g_gb_write を進めない → 同じフレームを繰り返し表示
        }

        // ポーズ中はゲーム進行をスキップ（描画ディスパッチの後に判定）
        if (g_menu_active) continue;

        // ジョイパッド合成: タッチ D-Pad + ゲームボタン + POWER ボタン(A)
        uint8_t joy = compute_dpad_joy() & s_btn_joy;
        if (gpio_get(SYS_OUT_PIN)) joy &= ~JOYPAD_A;
        gb_core_set_joypad(joy);
        gb_core_run_frame();

        // APU → リングバッファ（Bresenham で 32000Hz を維持）
        gb_core_fill_audio(s_apu_buf);
        s_bres_acc += BRES_ADDEND;
        int extra = 0;
        if (s_bres_acc >= BRES_THRESH) {
            s_bres_acc -= BRES_THRESH;
            extra = 1;
        }
        afifo_push_apu(s_apu_buf, GB_AUDIO_SAMPLES, extra);

        // SRAM 自動セーブ
        if (gb_core_consume_dirty()) g_sram_dirty_countdown = 60;
        if (g_sram_dirty_countdown > 0 && --g_sram_dirty_countdown == 0
                && gb_core_save_size() > 0) {
            g_storage_icon_color = COL_ICON_FLASH;
            multicore_lockout_start_blocking();
            save_flash_sram_save(gb_core_cart_ram_ptr(), gb_core_save_size());
            multicore_lockout_end_blocking();
            g_storage_icon_color = 0;
            printf("SRAM auto-saved\n");
        }

        // ステータスメッセージカウントダウン
        if (g_status_ttl > 0) --g_status_ttl;
    }
}
