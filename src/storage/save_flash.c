#include "save_flash.h"
#include "flash_meta.h"
#include "rom_flash.h"
#include "emu/gb/gb_core.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include <string.h>

static int slot_to_idx(int slot) {
    if (slot >= 0 && slot < SAVE_FLASH_STATE_N_SLOTS) return slot;
    return -1;
}

// Flash へ書き込む（セクタ消去 → ページプログラム）。
// 呼び出し前に Core 1 をロックアウト済みであること。
static void flash_write(uint32_t flash_offset, const uint8_t *src, size_t size) {
    static uint8_t page_buf[FLASH_SECTOR_SIZE];  // SRAM 上の作業バッファ
    size_t n_sectors = (size + FLASH_SECTOR_SIZE - 1) / FLASH_SECTOR_SIZE;
    for (size_t i = 0; i < n_sectors; i++) {
        size_t off   = i * FLASH_SECTOR_SIZE;
        size_t chunk = size - off;
        if (chunk > FLASH_SECTOR_SIZE) chunk = FLASH_SECTOR_SIZE;
        memcpy(page_buf, src + off, chunk);
        if (chunk < FLASH_SECTOR_SIZE)
            memset(page_buf + chunk, 0xFF, FLASH_SECTOR_SIZE - chunk);
        uint32_t ints = save_and_disable_interrupts();
        flash_range_erase  (flash_offset + off, FLASH_SECTOR_SIZE);
        flash_range_program(flash_offset + off, page_buf, FLASH_SECTOR_SIZE);
        restore_interrupts(ints);
    }
}

// ─── SRAM セーブ ──────────────────────────────────────────────────────────────

void save_flash_sram_save(const uint8_t *ram, size_t size) {
    if (size == 0 || size > SAVE_FLASH_SRAM_SIZE) return;
    flash_write(SAVE_FLASH_SRAM_OFFSET, ram, size);
    flash_meta_set_sram(rom_flash_ptr() + 0x0134);
}

int save_flash_sram_load(uint8_t *ram, size_t size) {
    if (size == 0) return 1;
    // マジック + ROM タイトル照合で有効性確認（ゴミデータや別 ROM のセーブを弾く）
    if (!flash_meta_sram_valid(rom_flash_ptr() + 0x0134)) return 1;
    const uint8_t *ptr = (const uint8_t *)(XIP_BASE + SAVE_FLASH_SRAM_OFFSET);
    memcpy(ram, ptr, size);
    return 0;
}

// ─── セーブステート ────────────────────────────────────────────────────────────

// ステートデータを格納する静的バッファ（BSS、SRAM 上）
static uint8_t s_state_buf[SAVE_FLASH_STATE_SLOT_SZ];

void save_flash_state_save(int slot) {
    int idx = slot_to_idx(slot);
    if (idx < 0) return;
    size_t state_size = gb_core_state_size();
    if (state_size > SAVE_FLASH_STATE_SLOT_SZ) return;
    gb_core_state_save(s_state_buf);
    uint32_t offset = SAVE_FLASH_STATE_OFFSET + (uint32_t)idx * SAVE_FLASH_STATE_SLOT_SZ;
    flash_write(offset, s_state_buf, state_size);
    flash_meta_set_state(slot);
}

int save_flash_state_load(int slot) {
    int idx = slot_to_idx(slot);
    if (idx < 0) return -1;
    if (!flash_meta_state_valid(slot)) return 1;  // フラグなし = 未保存
    const uint8_t *ptr = (const uint8_t *)(XIP_BASE + SAVE_FLASH_STATE_OFFSET
                         + (uint32_t)idx * SAVE_FLASH_STATE_SLOT_SZ);
    gb_core_state_load(ptr);
    return 0;
}
