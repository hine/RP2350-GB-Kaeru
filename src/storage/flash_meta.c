#include "flash_meta.h"
#include "rom_flash.h"
#include "save_flash.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include <string.h>

#define META_MAGIC_ROM    0x524F4D4Bu  // 'ROMK'
#define META_MAGIC_SRAM   0x53524D4Bu  // 'SRMK'
#define META_MAGIC_SET    0x53455454u  // 'SETT'
#define META_MAGIC_STATES 0x53544154u  // 'STAT'
#define ROM_TITLE_LEN    11

typedef struct {
    uint32_t rom_magic;               // META_MAGIC_ROM if valid
    uint8_t  rom_title[ROM_TITLE_LEN];
    uint8_t  _pad0[1];
    uint32_t sram_magic;              // META_MAGIC_SRAM if valid
    uint8_t  sram_rom_title[ROM_TITLE_LEN];
    uint8_t  _pad1[1];
} meta_t;  // 32 bytes

typedef struct {
    uint32_t magic;        // META_MAGIC_SET if valid
    uint8_t  palette_idx;
    uint8_t  audio_en;
    uint8_t  backlight;
    uint8_t  _pad;
} settings_t;  // 8 bytes

typedef struct {
    uint32_t magic;        // META_MAGIC_STATES if any slot was ever saved
    uint16_t valid_mask;   // bit i = 1 if slot i has valid data
    uint8_t  _pad[2];
} states_meta_t;  // 8 bytes

#define SETTINGS_OFFSET     sizeof(meta_t)                         // 32
#define STATES_META_OFFSET  (SETTINGS_OFFSET + sizeof(settings_t)) // 40

static const meta_t *meta_xip(void) {
    return (const meta_t *)(XIP_BASE + FLASH_META_OFFSET);
}

// セクタ内の一部分だけ上書きして書き戻す（残りの内容を保持する）
static void sector_rw(size_t offset, const void *data, size_t len) {
    static uint8_t buf[FLASH_SECTOR_SIZE];
    memcpy(buf, (const uint8_t *)(XIP_BASE + FLASH_META_OFFSET), FLASH_SECTOR_SIZE);
    memcpy(buf + offset, data, len);
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(FLASH_META_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(FLASH_META_OFFSET, buf, FLASH_SECTOR_SIZE);
    restore_interrupts(ints);
}

static void meta_write(const meta_t *m) {
    sector_rw(0, m, sizeof(meta_t));
}

bool flash_meta_rom_valid(void) {
    return meta_xip()->rom_magic == META_MAGIC_ROM;
}

void flash_meta_set_rom(const uint8_t *rom_title_11) {
    meta_t m;
    memcpy(&m, meta_xip(), sizeof(m));
    m.rom_magic = META_MAGIC_ROM;
    memcpy(m.rom_title, rom_title_11, ROM_TITLE_LEN);
    meta_write(&m);
}

void flash_meta_clear_rom(void) {
    meta_t m;
    memcpy(&m, meta_xip(), sizeof(m));
    m.rom_magic = 0xFFFFFFFFu;
    memset(m.rom_title, 0xFF, ROM_TITLE_LEN);
    // ROM クリア時は SRAM も無効化（ROM が変わるため古いセーブが無意味になる）
    m.sram_magic = 0xFFFFFFFFu;
    memset(m.sram_rom_title, 0xFF, ROM_TITLE_LEN);
    meta_write(&m);
}

bool flash_meta_sram_valid(const uint8_t *rom_title_11) {
    const meta_t *m = meta_xip();
    if (m->sram_magic != META_MAGIC_SRAM) return false;
    return memcmp(m->sram_rom_title, rom_title_11, ROM_TITLE_LEN) == 0;
}

void flash_meta_set_sram(const uint8_t *rom_title_11) {
    meta_t m;
    memcpy(&m, meta_xip(), sizeof(m));
    m.sram_magic = META_MAGIC_SRAM;
    memcpy(m.sram_rom_title, rom_title_11, ROM_TITLE_LEN);
    meta_write(&m);
}

void flash_meta_clear_sram(void) {
    meta_t m;
    memcpy(&m, meta_xip(), sizeof(m));
    m.sram_magic = 0xFFFFFFFFu;
    memset(m.sram_rom_title, 0xFF, ROM_TITLE_LEN);
    meta_write(&m);
}

// ── ユーザー設定 ──────────────────────────────────────────────────────────────

void flash_settings_load(uint8_t *palette_idx, uint8_t *audio_en, uint8_t *backlight) {
    const settings_t *s = (const settings_t *)(
        XIP_BASE + FLASH_META_OFFSET + SETTINGS_OFFSET);
    if (s->magic == META_MAGIC_SET) {
        *palette_idx = s->palette_idx;
        *audio_en    = s->audio_en;
        *backlight   = s->backlight;
    } else {
        *palette_idx = 0;
        *audio_en    = 1;
        *backlight   = 128;
    }
}

void flash_settings_save(uint8_t palette_idx, uint8_t audio_en, uint8_t backlight) {
    settings_t s;
    s.magic       = META_MAGIC_SET;
    s.palette_idx = palette_idx;
    s.audio_en    = audio_en;
    s.backlight   = backlight;
    s._pad        = 0;
    sector_rw(SETTINGS_OFFSET, &s, sizeof(settings_t));
}

void flash_meta_clear_all(void) {
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(FLASH_META_OFFSET, FLASH_SECTOR_SIZE);
    restore_interrupts(ints);
}

// ── セーブステートメタデータ ──────────────────────────────────────────────────

static const states_meta_t *states_xip(void) {
    return (const states_meta_t *)(XIP_BASE + FLASH_META_OFFSET + STATES_META_OFFSET);
}

bool flash_meta_state_valid(int slot) {
    if (slot < 0 || slot >= 10) return false;
    const states_meta_t *s = states_xip();
    if (s->magic != META_MAGIC_STATES) return false;
    return (s->valid_mask >> slot) & 1u;
}

void flash_meta_set_state(int slot) {
    if (slot < 0 || slot >= 10) return;
    states_meta_t s;
    const states_meta_t *cur = states_xip();
    if (cur->magic == META_MAGIC_STATES) {
        s = *cur;
    } else {
        s.magic      = META_MAGIC_STATES;
        s.valid_mask = 0;
        s._pad[0]    = 0;
        s._pad[1]    = 0;
    }
    s.valid_mask |= (uint16_t)(1u << slot);
    sector_rw(STATES_META_OFFSET, &s, sizeof(states_meta_t));
}

// ── 完全消去 ──────────────────────────────────────────────────────────────────

void flash_full_erase(void) {
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(ROM_FLASH_OFFSET,
                      ROM_FLASH_MAX_SIZE);
    flash_range_erase(SAVE_FLASH_SRAM_OFFSET,
                      SAVE_FLASH_SRAM_SIZE);
    flash_range_erase(SAVE_FLASH_STATE_OFFSET,
                      SAVE_FLASH_STATE_N_SLOTS * SAVE_FLASH_STATE_SLOT_SZ);
    flash_range_erase(FLASH_META_OFFSET,
                      FLASH_SECTOR_SIZE);
    restore_interrupts(ints);
}
