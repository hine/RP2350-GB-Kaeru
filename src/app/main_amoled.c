#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/i2c.h"
#include "hardware/clocks.h"
#include "boards/waveshare_touch_amoled_1_8/board_config.h"
#include "drivers/display/amoled_1in8.h"
#include "drivers/input/ft3168_touch.h"
#include "drivers/audio/es8311.h"
#include "drivers/audio/audio_i2s.h"
#include "emu/gb/gb_core.h"
#include "storage/rom_flash.h"
#include "storage/flash_meta.h"
#include "storage/save_flash.h"
#include "drivers/storage/storage_sd.h"

// ── 表示レイアウト ────────────────────────────────────────────────────────────
// 上部: ステータスバー 16px（セーブ UI）
// 中部: GB フレーム 160×144 → 2× = 320×288、中央寄せ
// 下部: 操作エリア 144px
#define STATUS_H  16
#define GB_SCALE  2
#define GB_W2     (GB_SCREEN_W * GB_SCALE)              // 320
#define GB_H2     (GB_SCREEN_H * GB_SCALE)              // 288
#define GB_X_OFF  ((AMOLED_WIDTH  - GB_W2) / 2)         // 24
#define GB_Y_OFF  STATUS_H                               // 16

// 操作エリア（STATUS_H + GB_H2 より下の 144px）
#define CTRL_Y    (GB_Y_OFF + GB_H2)                     // 304
#define CTRL_H    (AMOLED_HEIGHT - CTRL_Y)               // 144

// 左半分: D-Pad。中心点
#define DPAD_CX   (AMOLED_WIDTH / 4)                     // 92
#define DPAD_CY   (CTRL_Y + CTRL_H / 2)                  // 376
#define DPAD_DEAD 30

// 右半分: ボタン。4象限の境界線
#define BTN_MID_X (AMOLED_WIDTH  * 3 / 4)               // 276
#define BTN_MID_Y (CTRL_Y + CTRL_H / 2)                  // 376

// ステータスバーのタッチゾーン
#define STATUS_SAVE_X  (AMOLED_WIDTH / 3)               // 0..122 でセーブ
#define STATUS_LOAD_X  (AMOLED_WIDTH * 2 / 3)           // 245..367 でロード

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
static volatile bool     g_pwr_a_held = false;   // POWER ボタン A 保持フラグ

static int g_gb_write = 0;   // Core 0 が書き込む GB バッファ番号

// ── セーブ関連 ───────────────────────────────────────────────────────────────
static int  g_state_slot           = 0;   // 現在のセーブスロット (0-9)
static int  g_sram_dirty_countdown = 0;   // SRAM 自動セーブカウントダウン（60=~1秒）
static int  g_status_ttl           = 0;   // ステータスバーフラッシュ残フレーム数
static uint16_t g_status_flash_color = 0; // フラッシュ色

// ── 音声 SPSC リングバッファ ──────────────────────────────────────────────────
// Producer: Core 0 メインループ（APU サンプル変換後にプッシュ）
// Consumer: Core 0 DMA IRQ ハンドラ（audio_fill コールバック）
// SPSC なのでロック不要。wr は Producer のみ更新、rd は Consumer のみ更新。
#define AFIFO_SIZE  4096u   // 2の冪、~128ms @ 32000 Hz

static uint32_t          s_afifo[AFIFO_SIZE];
static volatile uint16_t s_afifo_wr = 0;
static volatile uint16_t s_afifo_rd = 0;

// Bresenham レートコレクション。
// APU が生成する 535 サンプル/フレームと ES8311 の 32000 Hz のズレを補正する。
// 目標 = 32000 × 70224 / 4194304 = 535 + 785/1024 サンプル/フレーム
// → フレームごとに 785 を加算し、1024 を超えたら無音サンプルを 1 つ追加。
#define BRES_ADDEND  785
#define BRES_THRESH  1024
static int s_bres_acc = 0;

// DMA IRQ から呼ばれる音声フィルコールバック（SRAM 配置必須）。
static void __not_in_flash_func(audio_fill)(uint32_t *dst, int n) {
    uint16_t rd    = s_afifo_rd;
    uint16_t wr    = s_afifo_wr;
    int      avail = (int)(uint16_t)(wr - rd);
    int      got   = (avail < n) ? avail : n;
    for (int i = 0; i < got; i++)
        dst[i] = s_afifo[(rd++) & (AFIFO_SIZE - 1u)];
    for (int i = got; i < n; i++)
        dst[i] = 0;   // アンダーランは無音で補完
    __dmb();
    s_afifo_rd = rd;
}

// APU サンプル（S16 ステレオ）→ PIO I2S フォーマット（L<<16|R）変換してプッシュ。
// Flash 書き込み中は呼ばれないので __not_in_flash_func 不要。
static void afifo_push_apu(const int16_t *src, int n_pairs, int extra_silence) {
    uint16_t wr    = s_afifo_wr;
    uint16_t rd    = s_afifo_rd;
    int      space = (int)(uint16_t)((uint16_t)(AFIFO_SIZE - 1u) - (wr - rd));
    int      push  = n_pairs + extra_silence;
    if (push > space) push = space;   // 溢れる分は捨てる
    for (int i = 0; i < push && i < n_pairs; i++) {
        int16_t l = src[2 * i];
        int16_t r = src[2 * i + 1];
        s_afifo[(wr++) & (AFIFO_SIZE - 1u)] =
            ((uint32_t)(uint16_t)l << 16) | (uint16_t)r;
    }
    for (int i = n_pairs; i < push; i++)
        s_afifo[(wr++) & (AFIFO_SIZE - 1u)] = 0;   // 無音補完
    __dmb();
    s_afifo_wr = wr;
}

// APU バッファ（スタックには大きすぎるので static）
static int16_t s_apu_buf[GB_AUDIO_SAMPLES_TOTAL];

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

// ── ステータスバー描画（毎フレーム Core 0 から更新可） ────────────────────────
static void draw_status_bar(void) {
    uint16_t bg   = AMOLED_COLOR(0x1082);  // 暗いグレー（通常時）
    uint16_t save_c = AMOLED_COLOR(0x2945);  // セーブゾーン: やや明るい
    uint16_t load_c = AMOLED_COLOR(0x2945);  // ロードゾーン: やや明るい

    // フラッシュ中は全体をフラッシュ色で上書き
    if (g_status_ttl > 0) {
        fb_fill(0, 0, AMOLED_WIDTH, STATUS_H, g_status_flash_color);
        return;
    }

    // 通常表示: セーブ | スロット×10 | ロード
    fb_fill(0, 0, AMOLED_WIDTH, STATUS_H, bg);
    fb_fill(0, 0, STATUS_SAVE_X, STATUS_H, save_c);
    fb_fill(STATUS_LOAD_X, 0, AMOLED_WIDTH, STATUS_H, load_c);

    // スロットインジケーター: 10個の小ブロック (中央エリア)
    int slot_area_w = STATUS_LOAD_X - STATUS_SAVE_X;  // ~122px
    int block_w = slot_area_w / SAVE_FLASH_STATE_N_SLOTS;  // 12px
    for (int i = 0; i < SAVE_FLASH_STATE_N_SLOTS; i++) {
        int x0 = STATUS_SAVE_X + i * block_w + 1;
        int x1 = x0 + block_w - 2;
        uint16_t c = (i == g_state_slot)
                     ? AMOLED_COLOR(0xFFFF)   // 現在スロット: 白
                     : AMOLED_COLOR(0x39E7);  // 他スロット: 暗め
        fb_fill(x0, 2, x1, STATUS_H - 2, c);
    }
}

// ── 操作 UI（起動時に一度だけ描画） ─────────────────────────────────────────
static void draw_controls(void) {
    // GB エリア左右余白: 黒（STATUS_H 分下にシフト）
    fb_fill(0, GB_Y_OFF, GB_X_OFF, GB_Y_OFF + GB_H2, AMOLED_COLOR(0x0000));
    fb_fill(GB_X_OFF + GB_W2, GB_Y_OFF, AMOLED_WIDTH, GB_Y_OFF + GB_H2, AMOLED_COLOR(0x0000));

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
    // 上段: システムボタン（地味な色）
    // SELECT: 左上 → 暗めの黄系
    fb_fill(AMOLED_WIDTH/2, CTRL_Y, BTN_MID_X, BTN_MID_Y, AMOLED_COLOR(0x3180));
    // START: 右上 → 暗めの緑系
    fb_fill(BTN_MID_X, CTRL_Y, AMOLED_WIDTH, BTN_MID_Y, AMOLED_COLOR(0x0200));
    // 下段: アクションボタン（明るい色）
    // B: 左下 → 青系
    fb_fill(AMOLED_WIDTH/2, BTN_MID_Y, BTN_MID_X, AMOLED_HEIGHT, AMOLED_COLOR(0x000A));
    // A: 右下 → 赤系
    fb_fill(BTN_MID_X, BTN_MID_Y, AMOLED_WIDTH, AMOLED_HEIGHT, AMOLED_COLOR(0x5000));

    // ボタン象限境界線
    fb_fill(BTN_MID_X, CTRL_Y, BTN_MID_X + 1, AMOLED_HEIGHT, AMOLED_COLOR(0x2945));
    fb_fill(AMOLED_WIDTH/2, BTN_MID_Y, AMOLED_WIDTH, BTN_MID_Y + 1, AMOLED_COLOR(0x2945));
}

// ── セーブ / ロード操作（Core 0 から呼ぶ、Core 1 はロックアウト済み） ─────────
static void do_save_state(void) {
    multicore_lockout_start_blocking();
    save_flash_state_save(g_state_slot);
    multicore_lockout_end_blocking();
    g_status_flash_color = AMOLED_COLOR(0xFFE0);  // 黄: セーブ完了
    g_status_ttl = 60;  // ~1秒間フラッシュ
    printf("State saved: slot %d\n", g_state_slot);
}

static void do_load_state(void) {
    int r = save_flash_state_load(g_state_slot);
    if (r == 0) {
        g_status_flash_color = AMOLED_COLOR(0x07E0);  // 緑: ロード成功
        printf("State loaded: slot %d\n", g_state_slot);
    } else {
        g_status_flash_color = AMOLED_COLOR(0xF800);  // 赤: 未保存
        printf("State load: slot %d empty\n", g_state_slot);
    }
    g_status_ttl = 45;
}

static void handle_status_touch(int tx) {
    if (tx < STATUS_SAVE_X) {
        do_save_state();
    } else if (tx >= STATUS_LOAD_X) {
        do_load_state();
    } else {
        // 中央: スロット切り替え（0-9 サイクル）
        g_state_slot = (g_state_slot + 1) % SAVE_FLASH_STATE_N_SLOTS;
        printf("Slot: %d\n", g_state_slot);
    }
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
        // 上段: SELECT(左) / START(右)
        // 下段: B(左)      / A(右)
        if ( upper &&  right) joy &= ~JOYPAD_START;
        if ( upper && !right) joy &= ~JOYPAD_SELECT;
        if (!upper &&  right) joy &= ~JOYPAD_A;
        if (!upper && !right) joy &= ~JOYPAD_B;
    }
    return joy;
}

// ── IRQ ──────────────────────────────────────────────────────────────────────
// FT3168 と POWER ボタンの GPIO IRQ を1つのハンドラに統合。
// pico-sdk は Core 単位でコールバックが1つだけ設定できる。
//
// POWER ボタン（GPIO18）のハードウェア挙動（実機確認）:
//   押下時に LOW パルス 1 回、離し時に LOW パルス 1 回 それぞれ発生する。
//   押している間ずっと LOW になるわけではない（ポーリングでは取れない）。
//   → FALLING edge ごとにトグルすることで「押下→A ON、離し→A OFF」を実現。
static void gpio_irq_handler(uint gpio, uint32_t events) {
    if (gpio == TOUCH_INT_PIN && (events & GPIO_IRQ_EDGE_RISE))
        g_touch_flag = true;
    if (gpio == SYS_OUT_PIN && (events & GPIO_IRQ_EDGE_FALL))
        g_pwr_a_held = !g_pwr_a_held;
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
        // ステータスバー (y=0..STATUS_H-1) + GB フレーム (y=GB_Y_OFF..CTRL_Y-1)
        // を全幅で一括転送する（ステータスバーの更新も含む）
        amoled_1in8_display_window(0, 0, AMOLED_WIDTH, CTRL_Y,
                                   (const uint16_t *)s_fb);
        __dmb();
        g_lcd_busy = false;
    }
}

// ── main ─────────────────────────────────────────────────────────────────────
int main(void) {
    // 200MHz にオーバークロック（RP2350 公式 150MHz だが 200MHz は一般的に安定動作）
    // GB フレームタイマーは crystal 由来のハードウェアタイマー → クロック変更の影響なし
    // 音声 MCLK 分周器は clock_get_hz(clk_sys) を実行時に読むので自動補正される
    set_sys_clock_khz(200 * 1000, true);
    clock_configure(clk_peri, 0,
        CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
        200000000, 200000000);

    stdio_init_all();
    sleep_ms(200);
    printf("\n=== AMOLED GB Kaeru ===\n");

    // I2C（FT3168 / ES8311 / QMI8658 共有バス）
    i2c_init(BOARD_I2C, BOARD_I2C_HZ);
    gpio_set_function(BOARD_I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(BOARD_I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(BOARD_I2C_SDA);
    gpio_pull_up(BOARD_I2C_SCL);

    // ES8311 音声コーデック初期化（I2C レジスタ設定のみ; MCLK は後で供給）
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

    // 初期画面（操作 UI を一度描画して表示）
    memset(s_fb, 0, sizeof(s_fb));
    draw_controls();
    amoled_1in8_display((const uint16_t *)s_fb);

    // ── ROM 準備（PicoCalc と同じフロー） ────────────────────────────────────
    // 優先度: 1) SD カード上の ROM → Flash 書き込み
    //         2) Flash 済み ROM をそのまま使用
    //         3) picotool で書き込んだ生データを自動検出
    #define ROM_PATH "0:/roms/kaeru.gb"

    bool rom_in_flash = flash_meta_rom_valid();
    bool sd_mounted   = false;

    printf("ROM: %s\n", rom_in_flash ? "Flash OK" : "SD required");

    if (!rom_in_flash) {
        // Flash に ROM なし → SD 必須。マウントできるまでリトライ
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
        // Flash に ROM あり → SD はオプション（1回だけ試みる）
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

    // picotool で生データを書き込んだ場合の自動検出
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
        printf("  Option A: SD card with roms/kaeru.gb\n");
        printf("  Option B: picotool load kaeru.gb -t bin -o 0x10100000\n");
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

    // SRAM セーブをロード（ROM タイトル照合で有効性確認）
    if (gb_core_save_size() > 0) {
        int sr = save_flash_sram_load(gb_core_cart_ram_ptr(), gb_core_save_size());
        printf("SRAM: %s\n", sr == 0 ? "loaded" : "no save");
    }

    // I2S DMA 音声ドライバ起動（Core 0 から呼ぶ → DMA IRQ を Core 0 に登録）
    // ES8311 は init 済み。audio_i2s_init() が MCLK を供給し ES8311 が BCLK/LRCLK を出力する。
    audio_i2s_set_fill_cb(audio_fill);
    audio_i2s_init();
    printf("Audio: OK\n");

    // POWER ボタン（GPIO18 = SYS_OUT_PIN）入力設定。
    // 押下時・離し時に LOW パルスが 1 回ずつ発生する（IRQ で検出）。
    // 【重要な制限】USB 給電中のみ安全。バッテリー使用時は長押し電源 OFF に注意。
    gpio_init(SYS_OUT_PIN);
    gpio_set_dir(SYS_OUT_PIN, GPIO_IN);
    gpio_pull_up(SYS_OUT_PIN);

    // FT3168 タッチ（ポイントモード）
    ft3168_init(BOARD_I2C, TOUCH_RST_PIN, FT3168_MODE_POINT);
    gpio_init(TOUCH_INT_PIN);
    gpio_pull_up(TOUCH_INT_PIN);
    gpio_set_dir(TOUCH_INT_PIN, GPIO_IN);

    // FT3168（EDGE_RISE）と POWER ボタン（EDGE_FALL）を同一ハンドラで受ける。
    // pico-sdk はコアごとに GPIO コールバックが 1 つだけ設定できる仕様のため統合。
    gpio_set_irq_enabled_with_callback(TOUCH_INT_PIN,
        GPIO_IRQ_EDGE_RISE, true, gpio_irq_handler);
    gpio_set_irq_enabled(SYS_OUT_PIN, GPIO_IRQ_EDGE_FALL, true);

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

        // タッチ読み取り
        if (g_touch_flag) {
            g_touch_flag = false;
            ft3168_read(BOARD_I2C, &touch);

            // ステータスバーのタッチ（セーブ / スロット切替 / ロード）
            if (touch.n_points > 0 && touch.p[0].event == 0) {
                // event==0: 新規プレスのみ受け付ける（押しっぱなしで連続実行しない）
                int ty = (int)touch.p[0].y;
                int tx = (int)touch.p[0].x;
                if (ty < STATUS_H) {
                    handle_status_touch(tx);
                }
            }
        }

        // タッチ + POWER 物理ボタンを合成してジョイパッドをセット。
        // g_pwr_a_held は IRQ ハンドラが FALLING edge ごとにトグルする。
        // タッチ A ゾーン（右下象限）も引き続き有効。
        uint8_t joy = joypad_from_touch(&touch);
        if (g_pwr_a_held) joy &= ~JOYPAD_A;
        gb_core_set_joypad(joy);
        gb_core_run_frame();

        // APU サンプルをリングバッファへ push
        // Bresenham で 535 + 785/1024 サンプル/フレーム ≈ 32000 Hz を維持
        gb_core_fill_audio(s_apu_buf);
        s_bres_acc += BRES_ADDEND;
        int extra = 0;
        if (s_bres_acc >= BRES_THRESH) {
            s_bres_acc -= BRES_THRESH;
            extra = 1;
        }
        afifo_push_apu(s_apu_buf, GB_AUDIO_SAMPLES, extra);

        // SRAM 自動セーブ: dirty 検出後 ~1秒デバウンスで Flash に保存
        if (gb_core_consume_dirty()) g_sram_dirty_countdown = 60;
        if (g_sram_dirty_countdown > 0 && --g_sram_dirty_countdown == 0
                && gb_core_save_size() > 0) {
            multicore_lockout_start_blocking();
            save_flash_sram_save(gb_core_cart_ram_ptr(), gb_core_save_size());
            multicore_lockout_end_blocking();
            printf("SRAM auto-saved\n");
        }

        // ステータスバーフラッシュカウントダウン
        if (g_status_ttl > 0) --g_status_ttl;

        // ステータスバーを更新してから Core 1 へ渡す
        draw_status_bar();

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
