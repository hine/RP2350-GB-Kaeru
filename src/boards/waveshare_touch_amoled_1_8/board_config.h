#pragma once

// Waveshare RP2350-Touch-AMOLED-1.8 pin assignments
// Source: DEV_Config.h + qspi_pio.h from Waveshare SDK

// I2C — shared by FT3168 touch and QMI8658 IMU
#define BOARD_I2C       i2c1
#define BOARD_I2C_SDA   6
#define BOARD_I2C_SCL   7
#define BOARD_I2C_HZ    400000

// FT3168 touch controller
#define TOUCH_RST_PIN   5
#define TOUCH_INT_PIN   4

// QSPI AMOLED display
#define DISP_CS_PIN     9
#define DISP_SCLK_PIN   10
#define DISP_DIO0_PIN   11
#define DISP_DIO1_PIN   12
#define DISP_DIO2_PIN   13
#define DISP_DIO3_PIN   14
#define DISP_PWR_EN_PIN 17
#define DISP_RST_PIN    15

// System
#define SYS_OUT_PIN     18  // active-high power key
