#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/clocks.h"
#include "boards/waveshare_touch_amoled_1_8/board_config.h"
#include "drivers/display/amoled_1in8.h"
#include "drivers/input/ft3168_touch.h"

// Full framebuffer: 368×448×2 bytes = 330 KB
// Touch markers and status are drawn directly into this buffer.
static uint16_t s_fb[AMOLED_HEIGHT][AMOLED_WIDTH];

// Big-endian RGB565 color constants (ready for DMA)
#define C_BG     AMOLED_COLOR(0x2104)  // dark gray
#define C_RED    AMOLED_COLOR(0xF800)
#define C_BLUE   AMOLED_COLOR(0x001F)
#define C_GREEN  AMOLED_COLOR(0x07E0)  // status: 1 finger
#define C_YELLOW AMOLED_COLOR(0xFFE0)  // status: 2 fingers
#define C_BLACK  AMOLED_COLOR(0x0000)  // status: 0 fingers

#define STATUS_H 40  // top status strip height

static volatile bool g_touch_flag = false;

static void touch_irq_cb(uint gpio, uint32_t events) {
    (void)events;
    if (gpio == TOUCH_INT_PIN) g_touch_flag = true;
}

static void fb_rect(int x, int y, int w, int h, uint16_t color) {
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = (x + w) > AMOLED_WIDTH  ? AMOLED_WIDTH  : (x + w);
    int y1 = (y + h) > AMOLED_HEIGHT ? AMOLED_HEIGHT : (y + h);
    for (int j = y0; j < y1; j++)
        for (int i = x0; i < x1; i++)
            s_fb[j][i] = color;
}

// Draw a crosshair (horizontal + vertical bar) centred at (cx, cy)
static void fb_cross(int cx, int cy, uint16_t color) {
    fb_rect(cx - 20, cy -  2, 40, 4, color);  // horizontal bar
    fb_rect(cx -  2, cy - 20, 4, 40, color);  // vertical bar
}

int main(void) {
    // System clock: 150 MHz (same as PicoCalc build)
    set_sys_clock_khz(150 * 1000, true);
    clock_configure(clk_peri, 0,
        CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
        150000000, 150000000);

    stdio_init_all();
    sleep_ms(200);
    printf("\n=== AMOLED 2-touch test ===\n");

    // I2C — shared by FT3168
    i2c_init(BOARD_I2C, BOARD_I2C_HZ);
    gpio_set_function(BOARD_I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(BOARD_I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(BOARD_I2C_SDA);
    gpio_pull_up(BOARD_I2C_SCL);

    // Display
    amoled_1in8_init();
    amoled_1in8_set_brightness(80);

    // Initial screen: dark background + black status strip
    fb_rect(0, 0, AMOLED_WIDTH, AMOLED_HEIGHT, C_BG);
    fb_rect(0, 0, AMOLED_WIDTH, STATUS_H, C_BLACK);
    amoled_1in8_display((const uint16_t *)s_fb);

    // FT3168 touch
    ft3168_init(BOARD_I2C, TOUCH_RST_PIN, FT3168_MODE_POINT);

    gpio_init(TOUCH_INT_PIN);
    gpio_pull_up(TOUCH_INT_PIN);
    gpio_set_dir(TOUCH_INT_PIN, GPIO_IN);
    gpio_set_irq_enabled_with_callback(TOUCH_INT_PIN,
        GPIO_IRQ_EDGE_RISE, true, touch_irq_cb);

    printf("Ready — touch the screen\n");

    // Track previous positions to erase them
    int      prev_n      = 0;
    uint16_t prev_x[2]   = {0, 0};
    uint16_t prev_y[2]   = {0, 0};
    uint16_t pt_color[2] = {C_RED, C_BLUE};
    uint16_t status_lut[3] = {C_BLACK, C_GREEN, C_YELLOW};

    while (true) {
        if (!g_touch_flag) {
            tight_loop_contents();
            continue;
        }
        g_touch_flag = false;

        ft3168_data_t td;
        ft3168_read(BOARD_I2C, &td);

        // Serial output for debugging
        printf("n=%d", td.n_points);
        for (int i = 0; i < td.n_points; i++)
            printf("  p%d=(%3d,%3d) ev=%d", i, td.p[i].x, td.p[i].y, td.p[i].event);
        printf("\n");

        // Erase old crosshairs
        for (int i = 0; i < prev_n; i++)
            fb_cross(prev_x[i], prev_y[i], C_BG);

        // Update status strip
        uint16_t sc = (td.n_points <= 2) ? status_lut[td.n_points] : C_BLACK;
        fb_rect(0, 0, AMOLED_WIDTH, STATUS_H, sc);

        // Draw new crosshairs
        int new_n = 0;
        for (int i = 0; i < td.n_points && i < 2; i++) {
            if (td.p[i].event == 1) continue;  // skip lift-up
            int cx = (int)td.p[i].x;
            int cy = (int)td.p[i].y;
            if (cy < STATUS_H) cy = STATUS_H;  // keep below status strip
            fb_cross(cx, cy, pt_color[i]);
            prev_x[new_n] = (uint16_t)cx;
            prev_y[new_n] = (uint16_t)cy;
            new_n++;
        }
        prev_n = new_n;

        amoled_1in8_display((const uint16_t *)s_fb);
    }
}
