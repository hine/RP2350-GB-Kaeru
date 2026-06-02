#include "hw_config.h"
#include "ff.h"
#include "diskio.h"

// SPI1: MISO=GP28, MOSI=GP27, SCK=GP26, CS=GP25
// ピン出典: Waveshare RP2350-Touch-AMOLED-1.8 ピン定義図
static spi_t spis[] = {
    {
        .hw_inst   = spi1,
        .miso_gpio = 28,
        .mosi_gpio = 27,
        .sck_gpio  = 26,
        .baud_rate = 12 * 1000 * 1000,
    }
};

static sd_card_t sd_cards[] = {
    {
        .pcName          = "0:",
        .spi             = &spis[0],
        .ss_gpio         = 25,
        .use_card_detect = false,
    }
};

size_t sd_get_num() { return count_of(sd_cards); }
sd_card_t *sd_get_by_num(size_t num) {
    if (num < sd_get_num()) return &sd_cards[num];
    return NULL;
}
size_t spi_get_num() { return count_of(spis); }
spi_t *spi_get_by_num(size_t num) {
    if (num < spi_get_num()) return &spis[num];
    return NULL;
}
