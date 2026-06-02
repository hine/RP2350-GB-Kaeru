#include "drivers/display/amoled_1in8.h"
#include "pico/stdlib.h"
#include "hardware/dma.h"

// DMA channel and config — owned by this module
static uint               s_dma_ch;
static dma_channel_config s_dma_cfg;

static void set_window(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1) {
    QSPI_Select(qspi);
    QSPI_REGISTER_Write(qspi, 0x2a);
    QSPI_DATA_Write(qspi, x0 >> 8);
    QSPI_DATA_Write(qspi, x0 & 0xff);
    QSPI_DATA_Write(qspi, (x1 - 1) >> 8);
    QSPI_DATA_Write(qspi, (x1 - 1) & 0xff);
    QSPI_Deselect(qspi);

    QSPI_Select(qspi);
    QSPI_REGISTER_Write(qspi, 0x2b);
    QSPI_DATA_Write(qspi, y0 >> 8);
    QSPI_DATA_Write(qspi, y0 & 0xff);
    QSPI_DATA_Write(qspi, (y1 - 1) >> 8);
    QSPI_DATA_Write(qspi, (y1 - 1) & 0xff);
    QSPI_Deselect(qspi);

    QSPI_Select(qspi);
    QSPI_REGISTER_Write(qspi, 0x2c);
    QSPI_Deselect(qspi);
    WAIT_TIME();
}

static void hw_reset(void) {
    gpio_put(qspi.pin_rst, 1);
    sleep_ms(50);
    gpio_put(qspi.pin_rst, 0);
    sleep_ms(50);
    gpio_put(qspi.pin_rst, 1);
    sleep_ms(300);
}

static void init_regs(void) {
    QSPI_Select(qspi);
    QSPI_REGISTER_Write(qspi, 0x11);
    sleep_ms(120);
    QSPI_Deselect(qspi);

    QSPI_Select(qspi); QSPI_REGISTER_Write(qspi, 0x44);
    QSPI_DATA_Write(qspi, 0x01); QSPI_DATA_Write(qspi, 0xC5);
    QSPI_Deselect(qspi);

    QSPI_Select(qspi); QSPI_REGISTER_Write(qspi, 0x35);
    QSPI_DATA_Write(qspi, 0x00); QSPI_Deselect(qspi);

    QSPI_Select(qspi); QSPI_REGISTER_Write(qspi, 0x3A);
    QSPI_DATA_Write(qspi, 0x55); QSPI_Deselect(qspi);  // RGB565

    QSPI_Select(qspi); QSPI_REGISTER_Write(qspi, 0xC4);
    QSPI_DATA_Write(qspi, 0x80); QSPI_Deselect(qspi);

    QSPI_Select(qspi); QSPI_REGISTER_Write(qspi, 0x53);
    QSPI_DATA_Write(qspi, 0x20); QSPI_Deselect(qspi);

    QSPI_Select(qspi); QSPI_REGISTER_Write(qspi, 0x51);
    QSPI_DATA_Write(qspi, 0xFF); QSPI_Deselect(qspi);

    QSPI_Select(qspi); QSPI_REGISTER_Write(qspi, 0x29);
    QSPI_Deselect(qspi);
    sleep_ms(10);
}

void amoled_1in8_init(void) {
    QSPI_GPIO_Init(qspi);
    QSPI_PIO_Init(qspi);
    QSPI_4Wrie_Mode(&qspi);

    // DMA — 8-bit transfers to PIO TX FIFO
    s_dma_ch  = dma_claim_unused_channel(true);
    s_dma_cfg = dma_channel_get_default_config(s_dma_ch);
    channel_config_set_transfer_data_size(&s_dma_cfg, DMA_SIZE_8);
    channel_config_set_read_increment(&s_dma_cfg, true);
    channel_config_set_write_increment(&s_dma_cfg, false);

    hw_reset();
    init_regs();
}

void amoled_1in8_set_brightness(uint8_t brightness) {
    if (brightness > 100) brightness = 100;
    uint8_t val = brightness * 255 / 100;
    QSPI_Select(qspi);
    QSPI_REGISTER_Write(qspi, 0x51);
    QSPI_DATA_Write(qspi, val);
    QSPI_Deselect(qspi);
}

void amoled_1in8_clear(uint16_t color_be) {
    // One row of pixels with the requested color
    static uint16_t row[AMOLED_WIDTH];
    for (int i = 0; i < AMOLED_WIDTH; i++) row[i] = color_be;

    set_window(0, 0, AMOLED_WIDTH, AMOLED_HEIGHT);
    QSPI_Select(qspi);
    QSPI_Pixel_Write(qspi, 0x2c);

    channel_config_set_dreq(&s_dma_cfg,
        pio_get_dreq(qspi.pio, qspi.sm, true));

    for (int y = 0; y < AMOLED_HEIGHT; y++) {
        dma_channel_configure(s_dma_ch, &s_dma_cfg,
            &qspi.pio->txf[qspi.sm],
            row, AMOLED_WIDTH * 2, true);
        while (dma_channel_is_busy(s_dma_ch));
    }
    QSPI_Deselect(qspi);
}

void amoled_1in8_display(const uint16_t *image) {
    set_window(0, 0, AMOLED_WIDTH, AMOLED_HEIGHT);
    QSPI_Select(qspi);
    QSPI_Pixel_Write(qspi, 0x2c);

    channel_config_set_dreq(&s_dma_cfg,
        pio_get_dreq(qspi.pio, qspi.sm, true));
    dma_channel_configure(s_dma_ch, &s_dma_cfg,
        &qspi.pio->txf[qspi.sm],
        image,
        AMOLED_WIDTH * AMOLED_HEIGHT * 2,
        true);
    while (dma_channel_is_busy(s_dma_ch));
    QSPI_Deselect(qspi);
}

void amoled_1in8_display_window(uint32_t x0, uint32_t y0,
                                uint32_t x1, uint32_t y1,
                                const uint16_t *image) {
    if (x1 > AMOLED_WIDTH)  x1 = AMOLED_WIDTH;
    if (y1 > AMOLED_HEIGHT) y1 = AMOLED_HEIGHT;

    set_window(x0, y0, x1, y1);
    QSPI_Select(qspi);
    QSPI_Pixel_Write(qspi, 0x2c);

    channel_config_set_dreq(&s_dma_cfg,
        pio_get_dreq(qspi.pio, qspi.sm, true));

    uint32_t row_bytes = (x1 - x0) * 2;
    for (uint32_t y = y0; y < y1; y++) {
        const uint8_t *row = (const uint8_t *)image + (y * AMOLED_WIDTH + x0) * 2;
        dma_channel_configure(s_dma_ch, &s_dma_cfg,
            &qspi.pio->txf[qspi.sm],
            row, row_bytes, true);
        while (dma_channel_is_busy(s_dma_ch));
    }
    QSPI_Deselect(qspi);
}
