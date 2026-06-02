#include "save_state.h"
#include "emu/gb/gb_core.h"
#include "ff.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define STATE_MAGIC   0x55524B45u   // "KERU" LE
#define STATE_VERSION 1u

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t state_size;    // sizeof(gb) + sizeof(apu_ctx)
    uint32_t cart_ram_size;
} state_hdr_t;

static void slot_path(char *buf, size_t sz, int slot)
{
    snprintf(buf, sz, "0:/saves/kaeru.s%d", slot);
}

int save_state_save(int slot)
{
    size_t state_sz = gb_core_state_size();
    void *buf = malloc(state_sz);
    if (!buf) return -1;
    gb_core_state_save(buf);

    f_mkdir("0:/saves");

    char path[32];
    slot_path(path, sizeof(path), slot);

    FIL fil;
    FRESULT fr = f_open(&fil, path, FA_WRITE | FA_CREATE_ALWAYS);
    if (fr != FR_OK) { free(buf); return -2; }

    UINT bw;
    state_hdr_t hdr = {
        .magic         = STATE_MAGIC,
        .version       = STATE_VERSION,
        .state_size    = (uint32_t)state_sz,
        .cart_ram_size = (uint32_t)gb_core_save_size(),
    };
    f_write(&fil, &hdr, sizeof(hdr), &bw);
    f_write(&fil, buf, state_sz, &bw);
    free(buf);

    if (gb_core_save_size() > 0 && gb_core_cart_ram_ptr())
        f_write(&fil, gb_core_cart_ram_ptr(), gb_core_save_size(), &bw);

    f_close(&fil);
    return 0;
}

int save_state_load(int slot)
{
    char path[32];
    slot_path(path, sizeof(path), slot);

    FIL fil;
    FRESULT fr = f_open(&fil, path, FA_READ);
    if (fr == FR_NO_FILE || fr == FR_NO_PATH) return 1;
    if (fr != FR_OK) return -2;

    state_hdr_t hdr;
    UINT br;
    f_read(&fil, &hdr, sizeof(hdr), &br);
    if (hdr.magic != STATE_MAGIC || hdr.version != STATE_VERSION) {
        f_close(&fil);
        return -3;
    }

    size_t state_sz = gb_core_state_size();
    if (hdr.state_size != (uint32_t)state_sz) {
        // ファームウェア更新でサイズが変わった場合はロード不可
        f_close(&fil);
        return -4;
    }

    void *buf = malloc(state_sz);
    if (!buf) { f_close(&fil); return -1; }

    f_read(&fil, buf, state_sz, &br);
    gb_core_state_load(buf);
    free(buf);

    if (hdr.cart_ram_size > 0 && gb_core_cart_ram_ptr()) {
        size_t read_sz = hdr.cart_ram_size < gb_core_save_size()
                       ? hdr.cart_ram_size : gb_core_save_size();
        f_read(&fil, gb_core_cart_ram_ptr(), read_sz, &br);
    }

    f_close(&fil);
    return 0;
}
