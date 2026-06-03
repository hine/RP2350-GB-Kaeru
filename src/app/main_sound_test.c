#include <stdio.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/clocks.h"
#include "boards/waveshare_touch_amoled_1_8/board_config.h"
#include "drivers/audio/es8311.h"
#include "drivers/audio/audio_i2s.h"

// ── Sine wave generator ───────────────────────────────────────────────────────
// 440 Hz tone (A4) at AUDIO_I2S_SAMPLE_RATE.
static float g_phase = 0.0f;
static const float PHASE_INC = 2.0f * 3.14159265f * 440.0f / AUDIO_I2S_SAMPLE_RATE;

// Called from DMA IRQ (Core 0).  Must be fast and SRAM-safe.
// Generates 440 Hz sine wave into both L and R channels.
static void __not_in_flash_func(fill_sine)(uint32_t *dst, int n) {
    for (int i = 0; i < n; i++) {
        int16_t s = (int16_t)(sinf(g_phase) * 8192.0f);  // ~-6 dBFS
        dst[i] = ((uint32_t)(uint16_t)s << 16) | (uint16_t)s;
        g_phase += PHASE_INC;
        if (g_phase >= 2.0f * 3.14159265f) g_phase -= 2.0f * 3.14159265f;
    }
}

int main(void) {
    set_sys_clock_khz(150 * 1000, true);
    clock_configure(clk_peri, 0,
        CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
        150000000, 150000000);

    stdio_init_all();
    sleep_ms(500);
    printf("\n=== AMOLED Sound Test (ES8311, 440 Hz) ===\n");

    // ── I2C (shared by ES8311, FT3168, QMI8658) ──────────────────────────────
    i2c_init(BOARD_I2C, BOARD_I2C_HZ);
    gpio_set_function(BOARD_I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(BOARD_I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(BOARD_I2C_SDA);
    gpio_pull_up(BOARD_I2C_SCL);

    // ── ES8311 init ───────────────────────────────────────────────────────────
    // MCLK frequency fed by audio_i2s_init() = AUDIO_I2S_SAMPLE_RATE * 192
    uint32_t mclk_hz = (uint32_t)AUDIO_I2S_SAMPLE_RATE * 192;
    es8311_init(BOARD_I2C, AUDIO_I2S_SAMPLE_RATE, mclk_hz);

    uint16_t chip_id = es8311_read_id(BOARD_I2C);
    // FD=chip_id(0x83), FE=version(0x11) → 0x1183 on this hardware
    printf("ES8311 chip ID: 0x%04X\n", chip_id);

    es8311_set_volume(BOARD_I2C, 80);
    es8311_mute(BOARD_I2C, false);

    // ── I2S DMA audio driver ──────────────────────────────────────────────────
    // Must be called from Core 0 (DMA IRQ registered on Core 0).
    audio_i2s_set_fill_cb(fill_sine);
    audio_i2s_init();

    printf("Playing 440 Hz sine wave. Press Ctrl-C to stop.\n\n");

    uint32_t t0 = to_ms_since_boot(get_absolute_time());
    while (true) {
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now - t0 >= 5000) {
            t0 = now;
            printf("Running: chip_id=0x%04X  MCLK=%u Hz  SR=%d Hz\n",
                   chip_id, mclk_hz, AUDIO_I2S_SAMPLE_RATE);
        }
        tight_loop_contents();
    }
}
