#include "save_sram.h"
#include "ff.h"

int save_sram_load(const char *path, uint8_t *ram, size_t size)
{
    if (!ram || size == 0) return 0;

    FIL fil;
    FRESULT fr = f_open(&fil, path, FA_READ);
    if (fr == FR_NO_FILE || fr == FR_NO_PATH) return 1;
    if (fr != FR_OK) return -(int)fr;

    UINT br;
    fr = f_read(&fil, ram, size, &br);
    f_close(&fil);
    return (fr == FR_OK) ? 0 : -(int)fr;
}

int save_sram_save(const char *path, const uint8_t *ram, size_t size)
{
    if (!ram || size == 0) return 0;

    f_mkdir("0:/saves");

    FIL fil;
    FRESULT fr = f_open(&fil, path, FA_WRITE | FA_CREATE_ALWAYS);
    if (fr != FR_OK) return -(int)fr;

    UINT bw;
    fr = f_write(&fil, ram, size, &bw);
    f_close(&fil);
    return (fr == FR_OK && bw == size) ? 0 : -(int)fr;
}
