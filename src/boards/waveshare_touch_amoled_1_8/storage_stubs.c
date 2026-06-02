#include "storage/rom_flash.h"
#include "hardware/flash.h"

// AMOLED ビルド向け ROM アクセススタブ。
// ROM は PicoCalc ビルドで Flash 書き込み済みの前提。SD カードは不使用。
// ROM_FLASH_OFFSET は rom_flash.h で定義済み（1MB）。

const uint8_t *rom_flash_ptr(void) {
    return (const uint8_t *)(XIP_BASE + ROM_FLASH_OFFSET);
}

// AMOLED ビルドでは SD から ROM を書き込まない
int rom_flash_ensure(const char *path) {
    (void)path;
    return -1;
}
