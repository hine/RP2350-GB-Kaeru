#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/clocks.h"
#include "boards/waveshare_touch_amoled_1_8/board_config.h"
#include "drivers/display/amoled_1in8.h"
#include "drivers/input/ft3168_touch.h"

// Full framebuffer: 368×448 px × 2 bytes
static uint16_t s_fb[AMOLED_HEIGHT][AMOLED_WIDTH];

// Big-endian RGB565
#define C(c)    AMOLED_COLOR(c)
#define C_GRAY   C(0x2104)
#define C_BLACK  C(0x0000)
#define C_WHITE  C(0xFFFF)
#define C_RED    C(0xF800)
#define C_GREEN  C(0x07E0)
#define C_BLUE   C(0x001F)
#define C_CYAN   C(0x07FF)
#define C_MAGENTA C(0xF81F)
#define C_YELLOW C(0xFFE0)
#define C_ORANGE C(0xFC00)

static volatile bool g_irq_flag = false;

static void touch_irq_cb(uint gpio, uint32_t events) {
    (void)events;
    if (gpio == TOUCH_INT_PIN) g_irq_flag = true;
}

static void fill_screen(uint16_t color) {
    for (int j = 0; j < AMOLED_HEIGHT; j++)
        for (int i = 0; i < AMOLED_WIDTH; i++)
            s_fb[j][i] = color;
}

// I2C ヘルパー（ドライバを介さず直接1バイト読み出し）
static uint8_t read_reg(uint8_t reg) {
    uint8_t val = 0;
    i2c_write_blocking(BOARD_I2C, FT3168_I2C_ADDR, &reg, 1, true);
    i2c_read_blocking(BOARD_I2C, FT3168_I2C_ADDR, &val, 1, false);
    return val;
}

int main(void) {
    set_sys_clock_khz(150 * 1000, true);
    clock_configure(clk_peri, 0,
        CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
        150000000, 150000000);

    stdio_init_all();
    sleep_ms(200);
    printf("\n=== FT3168 Gesture Mode Test ===\n");
    printf("Gestures: swipe up/down/left/right, tap, double-tap\n\n");

    // I2C
    i2c_init(BOARD_I2C, BOARD_I2C_HZ);
    gpio_set_function(BOARD_I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(BOARD_I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(BOARD_I2C_SDA);
    gpio_pull_up(BOARD_I2C_SCL);

    // Display
    amoled_1in8_init();
    amoled_1in8_set_brightness(80);
    fill_screen(C_GRAY);
    amoled_1in8_display((const uint16_t *)s_fb);

    // FT3168 — GESTURE MODE
    ft3168_init(BOARD_I2C, TOUCH_RST_PIN, FT3168_MODE_GESTURE);

    // INT: EDGE_RISE（Waveshare デモと同じ）
    gpio_init(TOUCH_INT_PIN);
    gpio_pull_up(TOUCH_INT_PIN);
    gpio_set_dir(TOUCH_INT_PIN, GPIO_IN);
    gpio_set_irq_enabled_with_callback(TOUCH_INT_PIN,
        GPIO_IRQ_EDGE_RISE, true, touch_irq_cb);

    printf("Ready. Try: swipe up/down/left/right, single tap, double tap\n\n");

    while (true) {
        if (!g_irq_flag) {
            tight_loop_contents();
            continue;
        }
        g_irq_flag = false;

        // reg 0xD3: Waveshare 独自ジェスチャーレジスタ
        uint8_t g_ws  = read_reg(0xD3);
        // reg 0x01: 標準ジェスチャーレジスタ（データシート記載）
        uint8_t g_std = read_reg(0x01);
        // reg 0x02: タッチ点数（ジェスチャー認識時に何が入るか確認用）
        uint8_t n     = read_reg(0x02) & 0x0F;

        // ── 色と名称を決定 ──────────────────────────────────────────────────
        uint16_t   bg_color;
        const char *name;

        switch ((ft3168_gesture_t)g_ws) {
            case FT3168_GESTURE_UP:           bg_color = C_GREEN;   name = "UP";           break;
            case FT3168_GESTURE_DOWN:         bg_color = C_BLUE;    name = "DOWN";         break;
            case FT3168_GESTURE_LEFT:         bg_color = C_MAGENTA; name = "LEFT";         break;
            case FT3168_GESTURE_RIGHT:        bg_color = C_CYAN;    name = "RIGHT";        break;
            case FT3168_GESTURE_CLICK:        bg_color = C_WHITE;   name = "CLICK";        break;
            case FT3168_GESTURE_DOUBLE_CLICK: bg_color = C_YELLOW;  name = "DOUBLE_CLICK"; break;
            case FT3168_GESTURE_NONE:
            default:
                bg_color = C_GRAY;
                name = (g_ws == 0) ? "NONE" : "UNKNOWN";
                break;
        }

        // ── 出力 ────────────────────────────────────────────────────────────
        printf("IRQ: 0xD3=%02X  0x01=%02X  n=%d  -> %s\n",
               g_ws, g_std, n, name);

        // 画面全体を色で塗り替え（ジェスチャーが視覚的に分かる）
        fill_screen(bg_color);
        amoled_1in8_display((const uint16_t *)s_fb);

        // NONE の場合は少し待ってからグレーに戻す
        if (g_ws == FT3168_GESTURE_NONE || name[0] == 'U') {
            // UNKNOWN/NONE は変化なし（すでに適切な色）
        } else {
            // 表示を 500ms 維持してからグレーに戻す
            sleep_ms(500);
            fill_screen(C_GRAY);
            amoled_1in8_display((const uint16_t *)s_fb);
        }
    }
}
