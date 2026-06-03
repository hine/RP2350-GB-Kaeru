#pragma once
#include <stdint.h>

// I2S sample rate used by this driver and the ES8311 configuration.
#define AUDIO_I2S_SAMPLE_RATE  32000

// Stereo samples per DMA buffer (~32 ms at 32000 Hz).
// Larger buffer = fewer IRQs = lower stutter risk.
#define AUDIO_I2S_BUF_SAMPLES  1024

// DMA fill callback.
// Called from DMA_IRQ_0 handler on Core 0 — must not block or touch Flash.
//
// dst       : write AUDIO_I2S_BUF_SAMPLES words here
// n_samples : always AUDIO_I2S_BUF_SAMPLES
//
// Word format: bits[31:16] = LEFT int16_t, bits[15:0] = RIGHT int16_t
//   e.g.  dst[i] = ((uint32_t)(uint16_t)(int16_t)l << 16) | (uint16_t)(int16_t)r;
typedef void (*audio_i2s_fill_cb_t)(uint32_t *dst, int n_samples);

// Register the fill callback before calling audio_i2s_init().
void audio_i2s_set_fill_cb(audio_i2s_fill_cb_t cb);

// Initialize PIO (MCLK + I2S out) and DMA.
// Must be called from Core 0 (DMA IRQ is registered on Core 0).
// ES8311 and I2C must be initialized before this call.
void audio_i2s_init(void);

// Stop DMA and disable PIOs.
// Call before multicore_reset_core1() or flash writes if audio is running.
void audio_i2s_stop(void);
