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
#include "drivers/display/amoled_ui_img.h"

// ── 表示レイアウト ────────────────────────────────────────────────────────────
// AMOLED: 368×448
//
//  y=  0..287  GB_H2    =288px  GB ゲーム画面（160×144 → 2×スケール）
//  y=288..303  STATUS_H = 16px  ステータスバー（背景黒）
//  y=304..447  UI_H     =144px  UI エリア（amoled_ui.png）
//              x=  0..143   D-Pad（固定中心 72,376）
//              x=144..223   システムボタン（MENU/SAVE/LOAD/SLOT 各 36px 高）
//              x=224..367   ゲームボタン（72×72×4、上段 SELECT/START、下段 B/A）

#define GB_SCALE     2
#define GB_W2        (GB_SCREEN_W * GB_SCALE)          // 320
#define GB_H2        (GB_SCREEN_H * GB_SCALE)          // 288
#define GB_X_OFF     ((AMOLED_WIDTH - GB_W2) / 2)      // 24
#define GB_Y_OFF     0

#define STATUS_Y     GB_H2                             // 288
#define STATUS_H     16
#define UI_Y         (STATUS_Y + STATUS_H)             // 304
#define UI_H         (AMOLED_HEIGHT - UI_Y)            // 144

#define DPAD_W       144
#define DPAD_CX      72                                // D-Pad 固定中心 X
#define DPAD_CY      (UI_Y + 72)                       // D-Pad 固定中心 Y (=376)
#define DPAD_DEAD    24

#define SYS_BTN_X    DPAD_W                            // 144
#define SYS_BTN_W    80
#define SYS_BTN_H    36                                // 各ボタン高さ

#define GAME_BTN_X   (SYS_BTN_X + SYS_BTN_W)          // 224
#define GAME_BTN_SZ  72                                // ボタン 1 個のサイズ

// ── UI カラー ─────────────────────────────────────────────────────────────────
#define COL_WHITE       AMOLED_COLOR(0xFFFF)

// ストレージアイコン色
#define COL_ICON_READ   AMOLED_COLOR(0xFFFF)   // 白: 読み込み中
#define COL_ICON_SD     AMOLED_COLOR(0x041F)   // 青: SD 書き込み
#define COL_ICON_FLASH  AMOLED_COLOR(0xFFE0)   // 黄: Flash 書き込み

// ── パレットテーブル（PicoCalc と共通 RGB 値） ────────────────────────────────
// 画像番号: 1,2,3,4,5,6,8,10,12,14
#define N_PALETTES 10
static const uint16_t s_palettes[N_PALETTES][4] = {
    // 0: DMGGreen   RGB: 9B/BC/0F  8B/AC/0F  30/62/30  0F/38/0F
    { AMOLED_COLOR(0x9DE1), AMOLED_COLOR(0x8D61), AMOLED_COLOR(0x3306), AMOLED_COLOR(0x09C1) },
    // 1: DarkGreen  RGB: 6A/8C/50  48/64/2C  24/38/10  08/14/04
    { AMOLED_COLOR(0x6C6A), AMOLED_COLOR(0x4B25), AMOLED_COLOR(0x21C2), AMOLED_COLOR(0x08A0) },
    // 2: Mono       RGB: FF/FF/FF  AA/AA/AA  55/55/55  00/00/00
    { AMOLED_COLOR(0xFFFF), AMOLED_COLOR(0xAD55), AMOLED_COLOR(0x52AA), AMOLED_COLOR(0x0000) },
    // 3: Sepia      RGB: F5/E6/C8  C4/96/6A  7A/55/32  2A/15/00
    { AMOLED_COLOR(0xF739), AMOLED_COLOR(0xC4AD), AMOLED_COLOR(0x7AA6), AMOLED_COLOR(0x28A0) },
    // 4: Amber      RGB: F8/E0/38  D8/98/00  8C/4C/00  38/18/00
    { AMOLED_COLOR(0xFF07), AMOLED_COLOR(0xDCC0), AMOLED_COLOR(0x8A60), AMOLED_COLOR(0x38C0) },
    // 5: CoolBlue   RGB: 94/C0/EC  50/84/C4  20/48/94  04/14/4C
    { AMOLED_COLOR(0x961D), AMOLED_COLOR(0x5438), AMOLED_COLOR(0x2252), AMOLED_COLOR(0x00A9) },
    // 6: Teal       RGB: 80/DC/C4  38/A0/8C  08/64/60  00/24/28
    { AMOLED_COLOR(0x86F8), AMOLED_COLOR(0x3D11), AMOLED_COLOR(0x0B2C), AMOLED_COLOR(0x0125) },
    // 7: Lavender   RGB: D4/C4/F4  98/80/D8  58/40/A8  18/08/50
    { AMOLED_COLOR(0xD63E), AMOLED_COLOR(0x9C1B), AMOLED_COLOR(0x5A15), AMOLED_COLOR(0x184A) },
    // 8: Rose       RGB: F0/B8/C8  D4/74/94  A0/34/58  44/04/20
    { AMOLED_COLOR(0xF5D9), AMOLED_COLOR(0xD3B2), AMOLED_COLOR(0xA1AB), AMOLED_COLOR(0x4024) },
    // 9: RedMono    RGB: FC/A4/9C  DC/58/50  A0/18/18  30/00/00
    { AMOLED_COLOR(0xFD33), AMOLED_COLOR(0xDACA), AMOLED_COLOR(0xA0C3), AMOLED_COLOR(0x3000) },
};
static const char *s_palette_names[N_PALETTES] = {
    "DMGGreen", "DarkGreen", "Mono", "Sepia",
    "Amber", "CoolBlue", "Teal", "Lavender", "Rose", "RedMono"
};
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

// D-Pad タッチ状態（Core 0 書き / Core 1 読み）
static volatile bool     g_dpad_active = false;
static volatile int16_t  g_dpad_cx = 0, g_dpad_cy = 0;

// ステータスバー（Core 0 書き / Core 1 読み）
static volatile char     g_status_msg[32];
static volatile int      g_status_ttl = 0;
static volatile uint16_t g_storage_icon_color = 0;

static int  g_gb_write   = 0;

#define SRAM_SD_PATH  "0:/saves/kaeru.sav"
static bool g_sd_mounted = false;

// ── メニュー / ポーズ ─────────────────────────────────────────────────────────
static bool                g_menu_active = false;
static repeating_timer_t   g_frame_timer;

// パレット選択
static int  g_palette_idx = 0;

// 音量ステップ: 0=Off, 1=20%, 2=40%, 3=60%, 4=80%, 5=100%
static int  g_vol_step    = 3;  // 起動時 60%
// ES8311 REG32 は 0.5 dB/step、0xBF(=191)が 0 dB 基準。
// 191 を超えると増幅になりクリッピングする（vol=80 相当の reg=203 が該当）。
// 等 dB 間隔（約 8 dB/step）で 100% でも reg=168（−11.5 dB）に収め歪みを回避。
//   20%=reg101(−45dB), 40%=reg119(−36dB), 60%=reg134(−28.5dB),
//   80%=reg152(−19.5dB), 100%=reg168(−11.5dB)
static const int  s_vol_values[6] = { 0, 40, 47, 53, 60, 66 };
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
static int     s_touch_zone = -1;   // -1=未タッチ, 0=上部, 1=D-Pad, 2=SysBtns, 3=GameBtns

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

// ── メニュー定数 ──────────────────────────────────────────────────────────────
// 7 項目: Palette / Volume / Backup to SD / Restore from SD /
//         Clear Flash / Reset / Close
#define MENU_N_ITEMS    7
#define MENU_TITLE_H    28                               // タイトルエリア高さ
#define MENU_ITEM_H     ((GB_H2 - MENU_TITLE_H) / MENU_N_ITEMS)  // 37
#define MENU_TITLE_Y    8
#define MENU_ITEMS_Y    MENU_TITLE_H
#define MENU_PANEL_X    GB_X_OFF
#define MENU_PANEL_W    GB_W2

#define MENU_BG         AMOLED_COLOR(0x1082)
#define MENU_ITEM_BG    AMOLED_COLOR(0x2104)
#define MENU_ITEM_SEL   AMOLED_COLOR(0x3186)
#define MENU_DIVIDER    AMOLED_COLOR(0x2945)
#define MENU_BORDER     AMOLED_COLOR(0xFFFF)

#define CONF_ORANGE_BG  AMOLED_COLOR(0x3940)  // Restore SD: オレンジ系
#define CONF_RED_BG     AMOLED_COLOR(0x4000)  // Clear Flash: 赤系

// 確認ダイアログ: ボックス・ボタン座標
#define CONF_BOX_X      (MENU_PANEL_X + 20)                    // 44
#define CONF_BOX_W      (MENU_PANEL_W - 40)                    // 280
#define CONF_BOX_Y      70
#define CONF_BOX_H      148
#define CONF_BTN_Y      (CONF_BOX_Y + CONF_BOX_H - 64)        // 154
#define CONF_BTN_H      54
#define CONF_BTN_NO_X   (CONF_BOX_X + 8)                       // 52
#define CONF_BTN_NO_W   ((CONF_BOX_W - 24) / 2)               // 128
#define CONF_BTN_YES_X  (CONF_BTN_NO_X + CONF_BTN_NO_W + 8)   // 188
#define CONF_BTN_YES_W  ((CONF_BOX_W - 24) / 2)               // 128

#define TOAST_BG        AMOLED_COLOR(0x0142)  // ダークグリーン
#define TOAST_BORDER    AMOLED_COLOR(0x064A)  // ブライトグリーン

typedef enum { MENU_STATE_ITEMS, MENU_STATE_CONFIRM, MENU_STATE_TOAST } menu_state_t;
static menu_state_t s_menu_state        = MENU_STATE_ITEMS;
static int          s_menu_item         = -1;
static int          s_confirm_item      = -1;
static bool         s_confirm_needs_lift = false; // 誤発火防止: 一度離してから受付
static char         s_toast_msg[32];
static int          s_toast_ttl         = 0;

// ── ステータスバー描画（Core 1 から毎フレーム呼ぶ） ─────────────────────────
static void draw_status_bar(void) {
    fb_fill(0, STATUS_Y, AMOLED_WIDTH, STATUS_Y + STATUS_H, 0x0000);

    if (g_status_ttl > 0) {
        char msg[32];
        memcpy(msg, (const void *)g_status_msg, sizeof(msg));
        fb_draw_text(4, STATUS_Y + (STATUS_H - 7) / 2, msg, COL_WHITE, 0x0000, 1);
    }

    uint16_t ic = g_storage_icon_color;
    if (ic != 0) {
        fb_fill(AMOLED_WIDTH - 13, STATUS_Y + 3, AMOLED_WIDTH - 3, STATUS_Y + STATUS_H - 3, ic);
    }
}

// ── メニューオーバーレイ描画（Core 1 から毎フレーム呼ぶ） ────────────────────
static void draw_menu_overlay(void) {
    static const char *labels[MENU_N_ITEMS] = {
        "Palette", "Volume", "Backup to SD",
        "Restore from SD", "Clear Flash", "Reset", "Close"
    };

    fb_fill(MENU_PANEL_X, 0, MENU_PANEL_X + MENU_PANEL_W, GB_H2, MENU_BG);
    fb_fill(MENU_PANEL_X, 0, MENU_PANEL_X + MENU_PANEL_W, 1, MENU_BORDER);
    fb_fill(MENU_PANEL_X, GB_H2 - 1, MENU_PANEL_X + MENU_PANEL_W, GB_H2, MENU_BORDER);

    fb_draw_text_center(MENU_PANEL_X, MENU_TITLE_Y, MENU_PANEL_W, "- MENU -",
                        COL_WHITE, MENU_BG, 2);
    fb_fill(MENU_PANEL_X, MENU_TITLE_H - 1, MENU_PANEL_X + MENU_PANEL_W,
            MENU_TITLE_H, MENU_DIVIDER);

    for (int i = 0; i < MENU_N_ITEMS; i++) {
        int iy  = MENU_ITEMS_Y + i * MENU_ITEM_H;
        int iy1 = iy + MENU_ITEM_H;
        if (iy1 > GB_H2) iy1 = GB_H2;
        uint16_t bg = (i == s_menu_item) ? MENU_ITEM_SEL : MENU_ITEM_BG;

        fb_fill(MENU_PANEL_X, iy, MENU_PANEL_X + MENU_PANEL_W, iy1, bg);
        if (i < MENU_N_ITEMS - 1)
            fb_fill(MENU_PANEL_X, iy1 - 1, MENU_PANEL_X + MENU_PANEL_W, iy1, MENU_DIVIDER);

        int ty = iy + (MENU_ITEM_H - 14) / 2;
        fb_draw_text(MENU_PANEL_X + 8, ty, labels[i], COL_WHITE, bg, 2);

        // パレット: 4色スウォッチ（現パレットのシェード0〜3）
        if (i == 0) {
            int sy = iy + (MENU_ITEM_H - 10) / 2;
            int sx = MENU_PANEL_X + MENU_PANEL_W - 8 - 4 * 12;
            for (int c = 0; c < 4; c++)
                fb_fill(sx + c * 12, sy, sx + c * 12 + 10, sy + 10,
                        s_palettes[g_palette_idx][c]);
        }
        // ボリューム: 値テキスト右寄せ
        if (i == 1) {
            int vw = (int)strlen(s_vol_labels[g_vol_step]) * 12;
            fb_draw_text(MENU_PANEL_X + MENU_PANEL_W - 8 - vw, ty,
                         s_vol_labels[g_vol_step], COL_WHITE, bg, 2);
        }
    }
}

// ── 確認ダイアログ描画（センター配置 No/Yes ボタン） ─────────────────────────
static void draw_menu_confirm(void) {
    bool is_clear = (s_confirm_item == 4);
    uint16_t yes_bg = is_clear ? CONF_RED_BG : CONF_ORANGE_BG;

    // 背景を暗くする
    fb_fill(MENU_PANEL_X, 0, MENU_PANEL_X + MENU_PANEL_W, GB_H2, MENU_BG);

    // ダイアログ本体
    int bx = CONF_BOX_X, bw = CONF_BOX_W;
    int by = CONF_BOX_Y, bh = CONF_BOX_H;
    fb_fill(bx, by, bx + bw, by + bh, MENU_ITEM_BG);
    // ボーダー
    fb_fill(bx,          by,          bx + bw,     by + 1,      MENU_BORDER);
    fb_fill(bx,          by + bh - 1, bx + bw,     by + bh,     MENU_BORDER);
    fb_fill(bx,          by,          bx + 1,       by + bh,     MENU_BORDER);
    fb_fill(bx + bw - 1, by,          bx + bw,     by + bh,     MENU_BORDER);

    // タイトル（scale=2）
    const char *title = is_clear ? "CLEAR FLASH?" : "RESTORE FROM SD?";
    fb_draw_text_center(bx, by + 12, bw, title, COL_WHITE, MENU_ITEM_BG, 2);

    // メッセージ（scale=1）
    const char *msg1 = is_clear ? "ROM / SRAM / Settings" : "Flash save will be";
    const char *msg2 = is_clear ? "will all be erased!"   : "overwritten.";
    fb_draw_text_center(bx, by + 40, bw, msg1, AMOLED_COLOR(0xAD55), MENU_ITEM_BG, 1);
    fb_draw_text_center(bx, by + 52, bw, msg2, AMOLED_COLOR(0xAD55), MENU_ITEM_BG, 1);

    // ボタン区切り線
    fb_fill(bx + 4, CONF_BTN_Y - 6, bx + bw - 4, CONF_BTN_Y - 5, MENU_DIVIDER);

    // No ボタン
    fb_fill(CONF_BTN_NO_X, CONF_BTN_Y,
            CONF_BTN_NO_X + CONF_BTN_NO_W, CONF_BTN_Y + CONF_BTN_H, MENU_BG);
    fb_fill(CONF_BTN_NO_X,                       CONF_BTN_Y,
            CONF_BTN_NO_X + CONF_BTN_NO_W,       CONF_BTN_Y + 1,      MENU_DIVIDER);
    fb_fill(CONF_BTN_NO_X,                       CONF_BTN_Y + CONF_BTN_H - 1,
            CONF_BTN_NO_X + CONF_BTN_NO_W,       CONF_BTN_Y + CONF_BTN_H, MENU_DIVIDER);
    fb_fill(CONF_BTN_NO_X,                       CONF_BTN_Y,
            CONF_BTN_NO_X + 1,                   CONF_BTN_Y + CONF_BTN_H, MENU_DIVIDER);
    fb_fill(CONF_BTN_NO_X + CONF_BTN_NO_W - 1,  CONF_BTN_Y,
            CONF_BTN_NO_X + CONF_BTN_NO_W,       CONF_BTN_Y + CONF_BTN_H, MENU_DIVIDER);

    // Yes ボタン
    fb_fill(CONF_BTN_YES_X, CONF_BTN_Y,
            CONF_BTN_YES_X + CONF_BTN_YES_W, CONF_BTN_Y + CONF_BTN_H, yes_bg);

    // ボタンテキスト（縦中央）
    int ty = CONF_BTN_Y + (CONF_BTN_H - 14) / 2;
    fb_draw_text_center(CONF_BTN_NO_X,  ty, CONF_BTN_NO_W,  "No",  COL_WHITE, MENU_BG,  2);
    fb_draw_text_center(CONF_BTN_YES_X, ty, CONF_BTN_YES_W, "Yes", COL_WHITE, yes_bg, 2);
}

// ── トースト描画（メニュー上に重ねる） ───────────────────────────────────────
static void draw_menu_toast(void) {
    int tx0 = MENU_PANEL_X + 20;
    int tx1 = MENU_PANEL_X + MENU_PANEL_W - 20;
    int ty0 = GB_H2 - 58;
    int ty1 = GB_H2 - 10;

    fb_fill(tx0, ty0, tx1, ty1, TOAST_BG);
    fb_fill(tx0, ty0,     tx1, ty0 + 1, TOAST_BORDER);
    fb_fill(tx0, ty1 - 1, tx1, ty1,     TOAST_BORDER);
    fb_fill(tx0, ty0, tx0 + 1, ty1, TOAST_BORDER);
    fb_fill(tx1 - 1, ty0, tx1, ty1, TOAST_BORDER);

    int th = ty1 - ty0;
    fb_draw_text_center(tx0 + 2, ty0 + (th - 14) / 2,
                        tx1 - tx0 - 4, s_toast_msg, COL_WHITE, TOAST_BG, 2);
}

// ── UI エリア描画（起動時のみ） ───────────────────────────────────────────────
static void draw_ui_area(void) {
    for (int y = 0; y < AMOLED_UI_IMG_H; y++)
        for (int x = 0; x < AMOLED_UI_IMG_W; x++)
            s_fb[UI_Y + y][x] = amoled_ui_img[y][x];
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
static void process_menu_touch(const ft3168_data_t *td);  // forward decl

static void do_menu_open(void) {
    g_menu_active  = true;
    g_dpad_active  = false;
    s_btn_joy      = 0xFF;
    s_menu_state   = MENU_STATE_ITEMS;
    s_menu_item    = -1;
    s_confirm_item = -1;
    strncpy((char *)g_status_msg, "PAUSED", sizeof(g_status_msg));
    g_status_ttl   = 9999;
    printf("Menu: open\n");
}

static void do_menu_close(void) {
    g_menu_active  = false;
    g_status_ttl   = 0;
    s_menu_state   = MENU_STATE_ITEMS;
    s_menu_item    = -1;
    s_confirm_item = -1;
    multicore_lockout_start_blocking();
    flash_settings_save((uint8_t)g_palette_idx, (uint8_t)g_vol_step, 80);
    multicore_lockout_end_blocking();
    printf("Menu: close, settings saved (pal=%d vol=%d)\n", g_palette_idx, g_vol_step);
}

// ── 確認アクション実行 ───────────────────────────────────────────────────────
static void execute_confirm_action(int item) {
    switch (item) {
        case 3: {  // Restore from SD
            bool ok = false;
            if (gb_core_save_size() > 0) {
                if (!g_sd_mounted) g_sd_mounted = (sd_mount() == FR_OK);
                if (g_sd_mounted) {
                    g_storage_icon_color = COL_ICON_READ;
                    int r = save_sram_load(SRAM_SD_PATH,
                                           gb_core_cart_ram_ptr(), gb_core_save_size());
                    if (r == 0) {
                        g_storage_icon_color = COL_ICON_FLASH;
                        multicore_lockout_start_blocking();
                        save_flash_sram_save(gb_core_cart_ram_ptr(), gb_core_save_size());
                        multicore_lockout_end_blocking();
                        ok = true;
                    }
                    g_storage_icon_color = 0;
                }
            }
            strncpy(s_toast_msg, ok ? "Restored!" : "Load failed", sizeof(s_toast_msg));
            s_toast_ttl    = 90;
            s_menu_item    = -1;
            s_confirm_item = -1;
            s_menu_state   = MENU_STATE_TOAST;
            printf("Restore: %s\n", s_toast_msg);
            break;
        }
        case 4: {  // Clear Flash
            g_storage_icon_color = COL_ICON_FLASH;
            multicore_lockout_start_blocking();
            flash_meta_clear_all();
            multicore_lockout_end_blocking();
            g_storage_icon_color = 0;
            strncpy(s_toast_msg, "Flash cleared!", sizeof(s_toast_msg));
            s_toast_ttl    = 90;
            s_menu_item    = -1;
            s_confirm_item = -1;
            s_menu_state   = MENU_STATE_TOAST;
            printf("Flash cleared\n");
            break;
        }
    }
}

// ── メニュー項目タップ処理 ───────────────────────────────────────────────────
static void handle_menu_item_tap(int item) {
    switch (item) {
        case 0:  // Palette: cycle
            g_palette_idx = (g_palette_idx + 1) % N_PALETTES;
            s_pal = s_palettes[g_palette_idx];
            break;
        case 1:  // Volume: cycle
            g_vol_step = (g_vol_step + 1) % 6;
            apply_volume();
            break;
        case 2: {  // Backup to SD
            bool ok = false;
            if (gb_core_save_size() > 0) {
                if (!g_sd_mounted) g_sd_mounted = (sd_mount() == FR_OK);
                if (g_sd_mounted) {
                    g_storage_icon_color = COL_ICON_SD;
                    ok = (save_sram_save(SRAM_SD_PATH,
                                        gb_core_cart_ram_ptr(), gb_core_save_size()) == 0);
                    g_storage_icon_color = 0;
                }
            }
            strncpy(s_toast_msg, ok ? "Saved to SD!" : "No save data", sizeof(s_toast_msg));
            s_toast_ttl  = 90;
            s_menu_item  = -1;
            s_menu_state = MENU_STATE_TOAST;
            break;
        }
        case 3:  // Restore from SD: 確認ダイアログへ
        case 4:  // Clear Flash: 確認ダイアログへ
            s_confirm_item       = item;
            s_menu_item          = -1;
            s_confirm_needs_lift = true;  // 指を離してから受付
            s_menu_state         = MENU_STATE_CONFIRM;
            break;
        case 5:  // Reset
            g_sram_dirty_countdown = 0;
            gb_core_reset();
            gb_core_set_fb(s_gb[g_gb_write]);
            gb_core_set_joypad(0xFF);
            if (gb_core_save_size() > 0)
                save_flash_sram_load(gb_core_cart_ram_ptr(), gb_core_save_size());
            do_menu_close();
            strncpy((char *)g_status_msg, "Reset", sizeof(g_status_msg));
            g_status_ttl = 90;
            break;
        case 6:  // Close
            s_menu_item = -1;
            do_menu_close();
            break;
    }
}

static void handle_sys_btn(int row) {
    switch (row) {
        case 0:
            if (g_menu_active) do_menu_close();
            else               do_menu_open();
            break;
        case 1: do_save_state(); break;
        case 2: do_load_state(); break;
        case 3:
            g_state_slot = (g_state_slot + 1) % SAVE_FLASH_STATE_N_SLOTS;
            snprintf((char *)g_status_msg, sizeof(g_status_msg), "Slot: %d", g_state_slot);
            g_status_ttl = 90;
            printf("Slot: %d\n", g_state_slot);
            break;
    }
}

// process_touch: IRQ 発火時にタッチ状態を更新する。
//
// FT3168 の実機挙動（確認済み）:
//   - 常に ev=2（接触継続）しか送らない。ev=0（押下開始）・ev=1（離し）は来ない。
//   - 「新規押下」検出: ゾーンが変わったとき (new_zone) に一度だけアクションを実行。
//   - 「指離れ」検出: IRQ が TOUCH_LIFT_FRAMES 以上来なくなったら離れたとみなす。
static void process_touch(const ft3168_data_t *td) {
    if (td->n_points == 0) return;

    if (g_menu_active) {
        int tx = (int)td->p[0].x;
        int ty = (int)td->p[0].y;
        // UIエリアの MENU ボタン（row=0）: メニューを閉じる
        if (ty >= UI_Y && tx >= SYS_BTN_X && tx < GAME_BTN_X) {
            if ((ty - UI_Y) / SYS_BTN_H == 0 && s_touch_zone != 2) {
                s_touch_zone = 2;
                do_menu_close();
            } else {
                s_touch_zone = 2;
            }
            return;
        }
        process_menu_touch(td);
        return;
    }

    int tx = (int)td->p[0].x;
    int ty = (int)td->p[0].y;

    // ゾーン判定（y < UI_Y はゲーム画面 + ステータスバー、タッチ無効）
    int zone;
    if      (ty < UI_Y)           zone = 0;  // ゲーム画面 + ステータス
    else if (tx < DPAD_W)         zone = 1;  // D-Pad
    else if (tx < GAME_BTN_X)     zone = 2;  // システムボタン
    else                          zone = 3;  // ゲームボタン

    bool new_zone = (zone != s_touch_zone);
    s_touch_zone = zone;

    switch (zone) {
        case 0:  // ゲーム画面 / ステータスバー: 何もしない
            g_dpad_active = false;
            s_btn_joy = 0xFF;
            break;

        case 1:  // D-Pad: タッチ座標をそのまま保持（固定中心との差で方向判定）
            g_dpad_active = true;
            g_dpad_cx = (int16_t)tx;
            g_dpad_cy = (int16_t)ty;
            s_btn_joy = 0xFF;
            break;

        case 2:  // システムボタン: ゾーン初入時のみ実行
            g_dpad_active = false;
            s_btn_joy = 0xFF;
            if (new_zone)
                handle_sys_btn((ty - UI_Y) / SYS_BTN_H);
            break;

        case 3:  // ゲームボタン: 押下状態を毎回更新
            g_dpad_active = false;
            {
                int bx = tx - GAME_BTN_X;
                int by = ty - UI_Y;
                s_btn_joy = 0xFF;
                if      (by < GAME_BTN_SZ && bx < GAME_BTN_SZ) s_btn_joy &= ~JOYPAD_SELECT;
                else if (by < GAME_BTN_SZ)                      s_btn_joy &= ~JOYPAD_START;
                else if (bx < GAME_BTN_SZ)                      s_btn_joy &= ~JOYPAD_B;
                else                                            s_btn_joy &= ~JOYPAD_A;
            }
            break;
    }
}

// ── メニュータッチ処理（状態マシン） ─────────────────────────────────────────
static void process_menu_touch(const ft3168_data_t *td) {
    if (td->n_points == 0) return;
    int tx = (int)td->p[0].x;
    int ty = (int)td->p[0].y;

    switch (s_menu_state) {

        case MENU_STATE_TOAST:
            // 任意タップでトースト即消し（新規タッチのみ）
            if (s_menu_item == -1) {
                s_menu_item          = 0;
                s_confirm_needs_lift = true;  // Items 復帰後に誤発火しない
                s_toast_ttl          = 0;
                s_menu_state         = MENU_STATE_ITEMS;
            }
            break;

        case MENU_STATE_CONFIRM: {
            // 指を離すまで受け付けない（誤発火防止）
            if (s_confirm_needs_lift) break;
            // 既に処理済みのタッチは無視
            if (s_menu_item != -1) break;

            // Yes / No ボタン範囲チェック
            bool in_yes = (tx >= CONF_BTN_YES_X &&
                           tx <  CONF_BTN_YES_X + CONF_BTN_YES_W &&
                           ty >= CONF_BTN_Y &&
                           ty <  CONF_BTN_Y + CONF_BTN_H);
            bool in_no  = (tx >= CONF_BTN_NO_X &&
                           tx <  CONF_BTN_NO_X + CONF_BTN_NO_W &&
                           ty >= CONF_BTN_Y &&
                           ty <  CONF_BTN_Y + CONF_BTN_H);

            if (in_yes) {
                s_menu_item = 1;
                execute_confirm_action(s_confirm_item);
            } else if (in_no) {
                s_menu_item          = 0;
                s_confirm_needs_lift = true;  // Items 復帰後に誤発火しない
                s_menu_state         = MENU_STATE_ITEMS;
                s_confirm_item       = -1;
            }
            // ボタン外タップは無視（明示的な操作を要求）
            break;
        }

        case MENU_STATE_ITEMS:
        default: {
            if (s_confirm_needs_lift) break;  // 前の状態のタッチが残っていれば無視
            if (ty < MENU_ITEMS_Y) break;
            int item = (ty - MENU_ITEMS_Y) / MENU_ITEM_H;
            if (item >= MENU_N_ITEMS) break;
            bool new_item = (item != s_menu_item);
            s_menu_item = item;
            if (!new_item) break;
            handle_menu_item_tap(item);
            break;
        }
    }
}

// 毎フレーム呼び出し: 固定中心からの変位で方向判定
static uint8_t compute_dpad_joy(void) {
    if (!g_dpad_active) return 0xFF;
    uint8_t joy = 0xFF;
    int dx = (int)g_dpad_cx - DPAD_CX;
    int dy = (int)g_dpad_cy - DPAD_CY;
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

        // ステータスバーを s_fb に反映
        draw_status_bar();

        // メニューオーバーレイ（ポーズ中）
        if (g_menu_active) {
            if (s_menu_state == MENU_STATE_CONFIRM)
                draw_menu_confirm();
            else {
                draw_menu_overlay();
                if (s_menu_state == MENU_STATE_TOAST)
                    draw_menu_toast();
            }
        }

        // ゲーム画面 + ステータスバーを転送（UI エリアは AMOLED GRAM に保持）
        amoled_1in8_display_window(0, 0, AMOLED_WIDTH, UI_Y,
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

    // 初期 UI 描画（UI エリアを GRAM に確定）
    memset(s_fb, 0, sizeof(s_fb));
    draw_ui_area();
    amoled_1in8_display((const uint16_t *)s_fb);

    // ── ROM 準備 ─────────────────────────────────────────────────────────────
    #define ROM_PATH "0:/roms/kaeru.gb"
    bool rom_in_flash = flash_meta_rom_valid();
    g_sd_mounted = false;

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
        g_sd_mounted = true;
    } else {
        sleep_ms(300);
        if (sd_mount() == FR_OK) {
            g_sd_mounted = true;
            printf("SD: OK\n");
        } else {
            printf("SD: (skip)\n");
        }
    }

    if (g_sd_mounted) {
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

    // 設定ロード
    {
        uint8_t pal, vol, bl;
        flash_settings_load(&pal, &vol, &bl);
        g_palette_idx = (pal < N_PALETTES) ? pal : 0;
        g_vol_step    = (vol < 6)          ? vol : 3;
        s_pal = s_palettes[g_palette_idx];
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
    apply_volume();

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

    // GB フレームタイマー（59.727fps = 16743μs）
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
            if (s_touch_age < TOUCH_LIFT_FRAMES) {
                s_touch_age++;
            }
            if (s_touch_age >= TOUCH_LIFT_FRAMES) {
                g_dpad_active        = false;
                s_btn_joy            = 0xFF;
                s_touch_zone         = -1;
                s_menu_item          = -1;
                s_confirm_needs_lift = false;
            }
        }

        // Core 1 へ描画ディスパッチ
        if (!g_lcd_busy) {
            g_lcd_idx = g_gb_write;
            __dmb();
            g_lcd_busy = true;
            if (!g_menu_active) {
                g_gb_write ^= 1;
                gb_core_set_fb(s_gb[g_gb_write]);
            }
        }

        if (g_menu_active) {
            // トースト自動消し
            if (s_toast_ttl > 0 && --s_toast_ttl == 0)
                s_menu_state = MENU_STATE_ITEMS;
            continue;
        }

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
