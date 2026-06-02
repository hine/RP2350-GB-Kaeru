#include <string.h>
#include <stdio.h>
#include "drivers/storage/storage_sd.h"
#include "ff.h"
#include "f_util.h"
#include "sd_card.h"
#include "spi.h"

static FATFS fs;
static bool mounted = false;

int sd_mount(void) {
    set_spi_dma_irq_channel(true, true);

    FRESULT fr = f_mount(&fs, "0:", 1);
    if (fr != FR_OK) return (int)fr;
    mounted = true;
    return FR_OK;
}

void sd_unmount(void) {
    f_unmount("0:");
    mounted = false;
}

bool sd_is_mounted(void) {
    return mounted;
}

bool sd_list_root(char *buf, size_t buf_size) {
    if (!mounted) return false;

    DIR dir;
    FILINFO fno;
    FRESULT fr = f_opendir(&dir, "/");
    if (fr != FR_OK) return false;

    buf[0] = '\0';
    size_t pos = 0;

    while (pos < buf_size - 1) {
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK || fno.fname[0] == '\0') break;

        int n = snprintf(buf + pos, buf_size - pos,
                         "%s%s\n",
                         (fno.fattrib & AM_DIR) ? "[D] " : "    ",
                         fno.fname);
        if (n < 0 || (size_t)n >= buf_size - pos) break;
        pos += n;
    }

    f_closedir(&dir);
    return true;
}
