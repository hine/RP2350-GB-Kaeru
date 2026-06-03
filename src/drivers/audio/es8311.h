#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "hardware/i2c.h"

// ES8311 I2C address (CE pin = low)
#define ES8311_I2C_ADDR  0x18

// Initialize ES8311 for DAC playback.
//   i2c        : I2C instance already initialized (e.g. i2c1)
//   sample_rate: playback rate in Hz — must be in the coefficient table (e.g. 32000)
//   mclk_hz    : MCLK frequency being supplied to ES8311 (e.g. 6144000)
void es8311_init(i2c_inst_t *i2c, uint32_t sample_rate, uint32_t mclk_hz);

// Set output volume 0 (silence) … 100 (full scale).
void es8311_set_volume(i2c_inst_t *i2c, int volume);

// Mute / unmute the DAC output.
void es8311_mute(i2c_inst_t *i2c, bool mute);

// Read chip ID — should return 0x0883 for ES8311.
uint16_t es8311_read_id(i2c_inst_t *i2c);
