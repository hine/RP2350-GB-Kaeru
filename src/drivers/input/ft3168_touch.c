#include "drivers/input/ft3168_touch.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include <stdio.h>

/* 3 ms: 13 bytes at 400 kHz takes ~0.4 ms; 3 ms gives 7× safety margin.
 * A timeout here means the I2C bus is stuck; return gracefully rather than
 * blocking Core 0 and freezing the menu. */
#define FT3168_I2C_TIMEOUT_US 3000

// Registers
#define REG_FINGER_NUM   0x02
#define REG_TOUCH1_XH    0x03  // through 0x0E covers both touch points
#define REG_POWER_MODE   0xA5
#define REG_GESTURE_MODE 0xD0
#define REG_DEVICE_ID    0xA0
#define REG_GESTURE_ID   0xD3  // Waveshare-specific gesture register

static void write_byte(i2c_inst_t *i2c, uint8_t reg, uint8_t val) {
    uint8_t buf[2] = {reg, val};
    i2c_write_blocking_until(i2c, FT3168_I2C_ADDR, buf, 2, false,
                             make_timeout_time_us(FT3168_I2C_TIMEOUT_US));
}

static uint8_t read_byte(i2c_inst_t *i2c, uint8_t reg) {
    uint8_t val = 0;
    int r = i2c_write_blocking_until(i2c, FT3168_I2C_ADDR, &reg, 1, true,
                                     make_timeout_time_us(FT3168_I2C_TIMEOUT_US));
    if (r < 0) return 0;
    i2c_read_blocking_until(i2c, FT3168_I2C_ADDR, &val, 1, false,
                            make_timeout_time_us(FT3168_I2C_TIMEOUT_US));
    return val;
}

static void read_bytes(i2c_inst_t *i2c, uint8_t reg, uint8_t *buf, size_t len) {
    int r = i2c_write_blocking_until(i2c, FT3168_I2C_ADDR, &reg, 1, true,
                                     make_timeout_time_us(FT3168_I2C_TIMEOUT_US));
    if (r < 0) return;
    i2c_read_blocking_until(i2c, FT3168_I2C_ADDR, buf, len, false,
                            make_timeout_time_us(FT3168_I2C_TIMEOUT_US));
}

static void do_reset(uint rst_pin) {
    gpio_put(rst_pin, 1); sleep_ms(20);
    gpio_put(rst_pin, 0); sleep_ms(20);
    gpio_put(rst_pin, 1); sleep_ms(50);
}

void ft3168_init(i2c_inst_t *i2c, uint rst_pin, ft3168_mode_t mode) {
    gpio_init(rst_pin);
    gpio_set_dir(rst_pin, GPIO_OUT);

    do_reset(rst_pin);

    write_byte(i2c, REG_POWER_MODE, 0x01);
    if (mode == FT3168_MODE_GESTURE)
        write_byte(i2c, REG_GESTURE_MODE, 0x01);
    sleep_ms(20);

    uint8_t id = read_byte(i2c, REG_DEVICE_ID);
    if (id != FT3168_DEVICE_ID)
        printf("FT3168: unexpected ID 0x%02X (expected 0x%02X)\n", id, FT3168_DEVICE_ID);
    else
        printf("FT3168: OK (ID=0x%02X)\n", id);
}

bool ft3168_read(i2c_inst_t *i2c, ft3168_data_t *out) {
    // Read 13 bytes: n_points + 2×(XH,XL,YH,YL,Weight,Misc)
    uint8_t buf[13];
    read_bytes(i2c, REG_FINGER_NUM, buf, sizeof(buf));

    uint8_t n = buf[0] & 0x0F;
    out->n_points = (n > 2) ? 2 : n;

    // Touch point 1 at buf[1..6]
    out->p[0].event = (buf[1] >> 6) & 0x03;
    out->p[0].x     = (uint16_t)((buf[1] & 0x0F) << 8) | buf[2];
    out->p[0].y     = (uint16_t)((buf[3] & 0x0F) << 8) | buf[4];

    // Touch point 2 at buf[7..12]
    out->p[1].event = (buf[7] >> 6) & 0x03;
    out->p[1].x     = (uint16_t)((buf[7] & 0x0F) << 8) | buf[8];
    out->p[1].y     = (uint16_t)((buf[9] & 0x0F) << 8) | buf[10];

    return out->n_points > 0;
}

ft3168_gesture_t ft3168_get_gesture(i2c_inst_t *i2c) {
    return (ft3168_gesture_t)read_byte(i2c, REG_GESTURE_ID);
}
