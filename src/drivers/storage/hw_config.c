#include "hw_config.h"
#include "ff.h"
#include "diskio.h"

// SPI0: MISO=GP16, CS=GP17, SCK=GP18, MOSI=GP19
static spi_t spis[] = {
    {
        .hw_inst = spi0,
        .miso_gpio = 16,
        .mosi_gpio = 19,
        .sck_gpio  = 18,
        .baud_rate = 12 * 1000 * 1000,
    }
};

// SD: CS=GP17, Detect=GP22
// card_detect は極性不明のため無効化してデバッグ中
static sd_card_t sd_cards[] = {
    {
        .pcName           = "0:",
        .spi              = &spis[0],
        .ss_gpio          = 17,
        .use_card_detect  = false,
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
