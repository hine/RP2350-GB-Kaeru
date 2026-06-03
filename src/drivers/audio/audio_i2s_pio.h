// Auto-generated PIO programs for ES8311 I2S audio (adapted from Waveshare SDK)
// MCLK  : pio1, configurable SM — generates master clock on MCLK_PIN (GPIO 22)
// I2S out: pio2, SM 0         — sends 16-bit stereo data; reads LRCLK(23)/BCLK(24) from ES8311

#pragma once

#if !PICO_NO_HARDWARE
#include "hardware/pio.h"
#endif

// ---------- //
// mclk_pio   //
// ---------- //
// 5-instruction square-wave generator.
// Clock div formula: div = sys_clk / (mclk_freq * 5)

#define mclk_pio_wrap_target 0
#define mclk_pio_wrap 4
#define mclk_pio_pio_version 1

static const uint16_t mclk_pio_program_instructions[] = {
    0xe001, //  0: set    pins, 1
    0xa042, //  1: nop
    0xe000, //  2: set    pins, 0
    0xa042, //  3: nop
    0x0000, //  4: jmp    0
};

#if !PICO_NO_HARDWARE
static const struct pio_program mclk_pio_program = {
    .instructions = mclk_pio_program_instructions,
    .length       = 5,
    .origin       = -1,
    .pio_version  = mclk_pio_pio_version,
#if PICO_PIO_VERSION > 0
    .used_gpio_ranges = 0x2
#endif
};

static inline pio_sm_config mclk_pio_program_get_default_config(uint offset) {
    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, offset + mclk_pio_wrap_target, offset + mclk_pio_wrap);
    return c;
}

static inline void mclk_pio_program_init(PIO pio, uint sm, uint offset, uint mclk_pin) {
    pio_sm_config c = mclk_pio_program_get_default_config(offset);
    sm_config_set_out_pins(&c, mclk_pin, 1);
    sm_config_set_set_pins(&c, mclk_pin, 1);
    pio_sm_init(pio, sm, offset, &c);
    uint mask = 1u << mclk_pin;
    pio_sm_set_pindirs_with_mask(pio, sm, mask, mask);
    pio_sm_set_pins(pio, sm, 0);
    pio_gpio_init(pio, mclk_pin);
}
#endif // !PICO_NO_HARDWARE

// ----------- //
// audio_pio   //
// ----------- //
// I2S slave output: reads BCLK(GPIO 24) and LRCLK(GPIO 23) from ES8311 (master).
// Each stereo sample = one 32-bit word: bits[31:16] = LEFT int16, bits[15:0] = RIGHT int16.
// First word after enable is consumed as a sync token (its value is discarded).
// NOTE: LRCLK and BCLK GPIO numbers are hardcoded in the bytecode below.

#define audio_pio_wrap_target 0
#define audio_pio_wrap 17
#define audio_pio_pio_version 1

static const uint16_t audio_pio_program_instructions[] = {
    0x80a0, //  0: pull   block          (sync token — consumed once at startup)
    0x2097, //  1: wait   1 gpio, 23     (LRCLK high)
    0x80a0, //  2: pull   block          (main loop: pull stereo word)
    0x2017, //  3: wait   0 gpio, 23     (LRCLK low  = LEFT channel start)
    0x2098, //  4: wait   1 gpio, 24     (BCLK high)
    0xe02f, //  5: set    x, 15
    0x2018, //  6: wait   0 gpio, 24     (BCLK falling edge)
    0x6001, //  7: out    pins, 1        (shift out 1 bit MSB-first)
    0x2098, //  8: wait   1 gpio, 24     (BCLK rising edge)
    0x0046, //  9: jmp    x--, 6         (16 bits → LEFT channel)
    0x2097, // 10: wait   1 gpio, 23     (LRCLK high = RIGHT channel start)
    0x2098, // 11: wait   1 gpio, 24
    0xe02f, // 12: set    x, 15
    0x2018, // 13: wait   0 gpio, 24
    0x6001, // 14: out    pins, 1
    0x2098, // 15: wait   1 gpio, 24
    0x004d, // 16: jmp    x--, 13        (16 bits → RIGHT channel)
    0x0002, // 17: jmp    2              (next stereo word)
};

#if !PICO_NO_HARDWARE
static const struct pio_program audio_pio_program = {
    .instructions = audio_pio_program_instructions,
    .length       = 18,
    .origin       = -1,
    .pio_version  = audio_pio_pio_version,
#if PICO_PIO_VERSION > 0
    .used_gpio_ranges = 0x2
#endif
};

static inline pio_sm_config audio_pio_program_get_default_config(uint offset) {
    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, offset + audio_pio_wrap_target, offset + audio_pio_wrap);
    return c;
}

// data_pin  : GPIO for I2S DATA OUT (output to ES8311 DAC input)
// lrclk_pin : GPIO for LRCLK (input from ES8311) — BCLK must be lrclk_pin + 1
static inline void audio_pio_program_init(PIO pio, uint sm, uint offset,
                                          uint data_pin, uint lrclk_pin) {
    pio_sm_config c = audio_pio_program_get_default_config(offset);
    sm_config_set_out_pins(&c, data_pin, 1);
    sm_config_set_out_shift(&c, false, false, 32); // left-shift, no auto-pull, 32-bit
    pio_sm_init(pio, sm, offset, &c);

    // data_pin = output; lrclk_pin, lrclk_pin+1 = inputs (driven by ES8311)
    uint pin_mask = (1u << data_pin) | (3u << lrclk_pin);
    uint pin_dirs = (1u << data_pin);
    pio_sm_set_pindirs_with_mask(pio, sm, pin_dirs, pin_mask);
    pio_gpio_init(pio, data_pin);
    pio_gpio_init(pio, lrclk_pin);
    pio_gpio_init(pio, lrclk_pin + 1);
}
#endif // !PICO_NO_HARDWARE
