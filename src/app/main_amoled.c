#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/i2c.h"
#include "hardware/clocks.h"
#include "boards/waveshare_touch_amoled_1_8/board_config.h"
#include "drivers/display/amoled_1in8.h"
#include "drivers/input/ft3168_touch.h"
#include "emu/gb/gb_core.h"
#include "storage/rom_flash.h"
#include "storage/flash_meta.h"

// ── 表示レイアウト ────────────────────────────────────────────────────────────
// GBフレーム 160×144 を 2× スケール → 320×288、中央寄せ
#define GB_SCALE  2
#define GB_W2     (GB_SCREEN_W * GB_SCALE)              // 320
#define GB_H2     (GB_SCREEN_H * GB_SCALE)              // 288
#define GB_X_OFF  ((AMOLED_WIDTH  - GB_W2) / 2)         // 24
#define GB_Y_OFF  0

// 操作エリア（画面下部 160px）
#define CTRL_Y    GB_H2                                  // 288
#define CTRL_H    (AMOLED_HEIGHT - CTRL_Y)               // 160

// 左半分: D-Pad。中心点
#define DPAD_CX   (AMOLED_WIDTH / 4)                     // 92
#define DPAD_CY   (CTRL_Y + CTRL_H / 2)                  // 368
#define DPAD_DEAD 30   // px: 中心からこれ以上離れて初めて入力とみなす

// 右半分: ボタン。4象限の境界線
#define BTN_MID_X (AMOLED_WIDTH  * 3 / 4)               // 276
#define BTN_MID_Y (CTRL_Y + CTRL_H / 2)                  // 368

// ── DMG Green パレット（big-endian RGB565） ──────────────────────────────────
static const uint16_t s_pal[4] = {
    AMOLED_COLOR(0x9DE1),   // 最明（#9BBC0F）
    AMOLED_COLOR(0x8D61),   // 中明（#8BAC0F）
    AMOLED_COLOR(0x3306),   // 中暗（#306230）
    AMOLED_COLOR(0x09C1),   // 最暗（#0F380F）
};

// ── フレームバッファ ─────────────────────────────────────────────────────────
// GB ピクセルインデックスダブルバッファ（Core 0 書き込み ↔ Core 1 読み出し）
static uint8_t  s_gb[2][GB_SCREEN_H][GB_SCREEN_W];
// AMOLED フレームバッファ（Core 1 が描画・転送する）
static uint16_t s_fb[AMOLED_HEIGHT][AMOLED_WIDTH];

// ── Core 間共有 ──────────────────────────────────────────────────────────────
static volatile uint8_t  g_joypad     = 0xFF;   // Core 0 が参照
static volatile int      g_lcd_idx    = 0;       // Core 1 が読む GB バッファ番号
static volatile bool     g_lcd_busy   = false;   // Core 1 が描画中
static volatile bool     g_frame_tick = false;   // フレームタイマー
static volatile bool     g_touch_flag = false;   // タッチ IRQ フラグ

static int g_gb_write = 0;   // Core 0 が書き込む GB バッファ番号

// ── 汎用描画 ─────────────────────────────────────────────────────────────────
static void fb_fill(int x0, int y0, int x1, int y1, uint16_t c) {
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > AMOLED_WIDTH)  x1 = AMOLED_WIDTH;
    if (y1 > AMOLED_HEIGHT) y1 = AMOLED_HEIGHT;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            s_fb[y][x] = c;
}

// ── 操作 UI（起動時に一度だけ描画） ─────────────────────────────────────────
static void draw_controls(void) {
    // GB エリア左右余白: 黒
    fb_fill(0, 0, GB_X_OFF, GB_H2, AMOLED_COLOR(0x0000));
    fb_fill(GB_X_OFF + GB_W2, 0, AMOLED_WIDTH, GB_H2, AMOLED_COLOR(0x0000));

    // 操作エリア全体の背景: 濃いネイビー
    fb_fill(0, CTRL_Y, AMOLED_WIDTH, AMOLED_HEIGHT, AMOLED_COLOR(0x0821));

    // 左右の境界線
    fb_fill(AMOLED_WIDTH/2, CTRL_Y, AMOLED_WIDTH/2 + 1, AMOLED_HEIGHT,
            AMOLED_COLOR(0x2945));

    // ── D-Pad エリア（左半分） ─────────────────────────────────────
    // 上下左右の方向ゾーンをわずかに色分け
    // 上ゾーン
    fb_fill(0, CTRL_Y, AMOLED_WIDTH/2, BTN_MID_Y - DPAD_DEAD,
            AMOLED_COLOR(0x0C41));
    // 下ゾーン
    fb_fill(0, BTN_MID_Y + DPAD_DEAD, AMOLED_WIDTH/2, AMOLED_HEIGHT,
            AMOLED_COLOR(0x0C41));
    // 左ゾーン
    fb_fill(0, CTRL_Y, DPAD_CX - DPAD_DEAD, AMOLED_HEIGHT,
            AMOLED_COLOR(0x0C41));
    // 右ゾーン
    fb_fill(DPAD_CX + DPAD_DEAD, CTRL_Y, AMOLED_WIDTH/2, AMOLED_HEIGHT,
            AMOLED_COLOR(0x0C41));

    // D-Pad クロスマーカー（水平バー + 垂直バー）
    fb_fill(DPAD_CX - 24, DPAD_CY - 3, DPAD_CX + 24, DPAD_CY + 3,
            AMOLED_COLOR(0x5ACB));
    fb_fill(DPAD_CX - 3, DPAD_CY - 24, DPAD_CX + 3, DPAD_CY + 24,
            AMOLED_COLOR(0x5ACB));

    // ── ボタンエリア（右半分）4象限 ───────────────────────────────
    // A: 右上 → 赤系
    fb_fill(BTN_MID_X, CTRL_Y, AMOLED_WIDTH, BTN_MID_Y, AMOLED_COLOR(0x5000));
    // B: 左上 → 青系
    fb_fill(AMOLED_WIDTH/2, CTRL_Y, BTN_MID_X, BTN_MID_Y, AMOLED_COLOR(0x000A));
    // START: 右下 → 緑系
    fb_fill(BTN_MID_X, BTN_MID_Y, AMOLED_WIDTH, AMOLED_HEIGHT, AMOLED_COLOR(0x0200));
    // SELECT: 左下 → 黄系
    fb_fill(AMOLED_WIDTH/2, BTN_MID_Y, BTN_MID_X, AMOLED_HEIGHT, AMOLED_COLOR(0x3180));

    // ボタン象限境界線
    fb_fill(BTN_MID_X, CTRL_Y, BTN_MID_X + 1, AMOLED_HEIGHT, AMOLED_COLOR(0x2945));
    fb_fill(AMOLED_WIDTH/2, BTN_MID_Y, AMOLED_WIDTH, BTN_MID_Y + 1, AMOLED_COLOR(0x2945));
}

// ── GB フレーム → AMOLED フレームバッファへ 2× スケール変換 ─────────────────
static void render_frame(const uint8_t src[GB_SCREEN_H][GB_SCREEN_W]) {
    for (int y = 0; y < GB_SCREEN_H; y++) {
        const uint8_t *row = src[y];
        int dy = GB_Y_OFF + y * 2;
        for (int x = 0; x < GB_SCREEN_W; x++) {
            uint16_t c  = s_pal[row[x] & 3];
            int      dx = GB_X_OFF + x * 2;
            s_fb[dy][dx]       = c;
            s_fb[dy][dx + 1]   = c;
            s_fb[dy + 1][dx]   = c;
            s_fb[dy + 1][dx + 1] = c;
        }
    }
}

// ── タッチ位置 → GB ジョイパッドビット ───────────────────────────────────────
static uint8_t joypad_from_touch(const ft3168_data_t *td) {
    uint8_t joy = 0xFF;
    if (td->n_points == 0 || td->p[0].event == 1) return joy;  // 無タッチ / 離し

    int tx = (int)td->p[0].x;
    int ty = (int)td->p[0].y;

    if (ty < CTRL_Y) return joy;  // ゲーム画面エリアはタッチ無効

    if (tx < AMOLED_WIDTH / 2) {
        // D-Pad ゾーン: 中心からの変位で方向を決定
        int dx = tx - DPAD_CX;
        int dy = ty - DPAD_CY;
        if (dy < -DPAD_DEAD) joy &= ~JOYPAD_UP;
        if (dy >  DPAD_DEAD) joy &= ~JOYPAD_DOWN;
        if (dx < -DPAD_DEAD) joy &= ~JOYPAD_LEFT;
        if (dx >  DPAD_DEAD) joy &= ~JOYPAD_RIGHT;
    } else {
        // ボタンゾーン: 4象限で A / B / START / SELECT
        bool upper = (ty < BTN_MID_Y);
        bool right = (tx >= BTN_MID_X);
        if (upper &&  right) joy &= ~JOYPAD_A;
        if (upper && !right) joy &= ~JOYPAD_B;
        if (!upper &&  right) joy &= ~JOYPAD_START;
        if (!upper && !right) joy &= ~JOYPAD_SELECT;
    }
    return joy;
}

// ── IRQ ──────────────────────────────────────────────────────────────────────
static void touch_irq(uint gpio, uint32_t events) {
    (void)gpio; (void)events;
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
        amoled_1in8_display_window(GB_X_OFF, GB_Y_OFF,
                                   GB_X_OFF + GB_W2, GB_Y_OFF + GB_H2,
                                   (const uint16_t *)s_fb);
        __dmb();
        g_lcd_busy = false;
    }
}

// ── main ─────────────────────────────────────────────────────────────────────
int main(void) {
    set_sys_clock_khz(150 * 1000, true);
    clock_configure(clk_peri, 0,
        CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
        150000000, 150000000);

    stdio_init_all();
    sleep_ms(200);
    printf("\n=== AMOLED GB Kaeru ===\n");

    // I2C（FT3168 用）
    i2c_init(BOARD_I2C, BOARD_I2C_HZ);
    gpio_set_function(BOARD_I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(BOARD_I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(BOARD_I2C_SDA);
    gpio_pull_up(BOARD_I2C_SCL);

    // AMOLED ディスプレイ初期化
    amoled_1in8_init();
    amoled_1in8_set_brightness(80);

    // 初期画面（操作 UI を一度描画して表示）
    memset(s_fb, 0, sizeof(s_fb));
    draw_controls();
    amoled_1in8_display((const uint16_t *)s_fb);

    // ROM 確認 ─── メタデータなしでも ROM データがあれば自動登録する
    // （picotool で直接 Flash 書き込みした場合のための自動検出）
    if (!flash_meta_rom_valid()) {
        const uint8_t *rom = rom_flash_ptr();
        // GB ROM ヘッダ 0x104 = Nintendo ロゴ先頭。0xFF なら未書き込み（Flash ブランク）
        if (rom[0x104] == 0xFF) {
            printf("ERROR: No ROM in Flash.\n");
            printf("Write ROM with:\n");
            printf("  picotool load kaeru.gb -t bin -o 0x10100000\n");
            printf("  picotool reboot\n");
            while (true) tight_loop_contents();
        }
        // ROM データあり → メタデータを自動登録（Core 1 未起動なのでロックアウト不要）
        printf("ROM data found, registering metadata...\n");
        flash_meta_set_rom(rom + 0x0134);  // GB タイトル（11バイト）
    }
    printf("ROM: OK\n");

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

    // FT3168 タッチ（ポイントモード）
    ft3168_init(BOARD_I2C, TOUCH_RST_PIN, FT3168_MODE_POINT);
    gpio_init(TOUCH_INT_PIN);
    gpio_pull_up(TOUCH_INT_PIN);
    gpio_set_dir(TOUCH_INT_PIN, GPIO_IN);
    gpio_set_irq_enabled_with_callback(TOUCH_INT_PIN,
        GPIO_IRQ_EDGE_RISE, true, touch_irq);

    // Core 1 起動（フレームレンダリング + AMOLED 転送担当）
    multicore_launch_core1(core1_main);

    // GB フレームタイマー（59.727fps = 16743μs）
    static repeating_timer_t frame_timer;
    add_repeating_timer_us(-16743, frame_timer_cb, NULL, &frame_timer);

    printf("Running.\n");

    static ft3168_data_t touch = {0};

    while (true) {
        while (!g_frame_tick) tight_loop_contents();
        g_frame_tick = false;

        // タッチ読み取り（IRQ フラグが立っているフレームのみ）
        if (g_touch_flag) {
            g_touch_flag = false;
            ft3168_read(BOARD_I2C, &touch);
        }

        gb_core_set_joypad(joypad_from_touch(&touch));
        gb_core_run_frame();

        // Core 1 が前フレームを描画し終えていれば新フレームを渡す
        if (!g_lcd_busy) {
            g_lcd_idx = g_gb_write;
            __dmb();
            g_lcd_busy  = true;
            g_gb_write ^= 1;
            gb_core_set_fb(s_gb[g_gb_write]);
        }
        // Core 1 がまだ描画中ならフレームドロップ（GB エミュは継続）
    }
}
