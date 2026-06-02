#pragma once

#include <stdint.h>
#include "drivers/display/qspi_pio.h"

#define AMOLED_WIDTH  368
#define AMOLED_HEIGHT 448

// RGB565 helpers — pixels must be stored big-endian in the framebuffer
#define AMOLED_COLOR(c) ((uint16_t)(((c) >> 8) | ((c) << 8)))

void amoled_1in8_init(void);
void amoled_1in8_set_brightness(uint8_t brightness);
void amoled_1in8_clear(uint16_t color_be);  // color already big-endian
void amoled_1in8_display(const uint16_t *image);
void amoled_1in8_display_window(uint32_t x0, uint32_t y0,
                                uint32_t x1, uint32_t y1,
                                const uint16_t *image);
