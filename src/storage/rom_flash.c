#include "rom_flash.h"
#include "hal/display.h"
#include "ff.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include <string.h>

// GB ROM ヘッダのタイトル領域（11 バイト）で変更検出する
#define ROM_TITLE_OFFSET 0x0134u
#define ROM_TITLE_LEN    11u

const uint8_t *rom_flash_ptr(void)
{
    return (const uint8_t *)(XIP_BASE + ROM_FLASH_OFFSET);
}

static bool rom_up_to_date(FIL *f)
{
    uint8_t sd_title[ROM_TITLE_LEN];
    UINT br;
    if (f_lseek(f, ROM_TITLE_OFFSET) != FR_OK) return false;
    if (f_read(f, sd_title, ROM_TITLE_LEN, &br) != FR_OK || br != ROM_TITLE_LEN) return false;
    f_lseek(f, 0);
    return memcmp(sd_title, rom_flash_ptr() + ROM_TITLE_OFFSET, ROM_TITLE_LEN) == 0;
}

static int rom_flash_write(const char *rom_path) {
    FIL f;
    if (f_open(&f, rom_path, FA_READ) != FR_OK) return -1;

    FSIZE_t rom_size = f_size(&f);
    if (rom_size > ROM_FLASH_MAX_SIZE) { f_close(&f); return -2; }

    uint32_t num_sectors = ((uint32_t)rom_size + FLASH_SECTOR_SIZE - 1) / FLASH_SECTOR_SIZE;
    lcd_print_string("Flashing ROM");

    static uint8_t buf[FLASH_SECTOR_SIZE];  // BSS に置く（SRAM 必須）
    for (uint32_t i = 0; i < num_sectors; i++) {
        UINT br;
        if (f_read(&f, buf, FLASH_SECTOR_SIZE, &br) != FR_OK) { f_close(&f); return -3; }
        if (br < FLASH_SECTOR_SIZE)
            memset(buf + br, 0xFF, FLASH_SECTOR_SIZE - br);
        uint32_t offs = ROM_FLASH_OFFSET + i * FLASH_SECTOR_SIZE;
        uint32_t ints = save_and_disable_interrupts();
        flash_range_erase(offs, FLASH_SECTOR_SIZE);
        flash_range_program(offs, buf, FLASH_SECTOR_SIZE);
        restore_interrupts(ints);
        if (i % 8 == 0) lcd_print_string(".");
    }
    lcd_print_string("\n");
    f_close(&f);
    return 0;
}

int rom_flash_ensure(const char *rom_path)
{
    FIL f;
    if (f_open(&f, rom_path, FA_READ) != FR_OK) return -1;
    bool up_to_date = rom_up_to_date(&f);
    f_close(&f);
    if (up_to_date) return 0;
    return rom_flash_write(rom_path);
}

int rom_flash_force(const char *rom_path)
{
    return rom_flash_write(rom_path);
}
