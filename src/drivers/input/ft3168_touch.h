#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "hardware/i2c.h"

#define FT3168_I2C_ADDR  0x38
#define FT3168_DEVICE_ID 0x03  // expected value at reg 0xA0

typedef enum {
    FT3168_MODE_POINT   = 1,
    FT3168_MODE_GESTURE = 2,
} ft3168_mode_t;

typedef enum {
    FT3168_GESTURE_NONE         = 0x00,
    FT3168_GESTURE_DOUBLE_CLICK = 0x24,
    FT3168_GESTURE_UP           = 0x22,
    FT3168_GESTURE_DOWN         = 0x23,
    FT3168_GESTURE_LEFT         = 0x20,
    FT3168_GESTURE_RIGHT        = 0x21,
    FT3168_GESTURE_CLICK        = 0x26,
} ft3168_gesture_t;

typedef struct {
    uint16_t x;
    uint16_t y;
    uint8_t  event;  // 0=press, 1=lift, 2=contact
} ft3168_point_t;

typedef struct {
    uint8_t        n_points;  // 0-2
    ft3168_point_t p[2];
} ft3168_data_t;

// Initialize: configures RST pin, resets chip, sets mode.
// Call after I2C bus is already initialized.
void ft3168_init(i2c_inst_t *i2c, uint rst_pin, ft3168_mode_t mode);

// Read up to 2 touch points in one I2C transaction.
// Returns true if at least one point is active.
bool ft3168_read(i2c_inst_t *i2c, ft3168_data_t *out);

// Read gesture register (valid in GESTURE mode).
ft3168_gesture_t ft3168_get_gesture(i2c_inst_t *i2c);
