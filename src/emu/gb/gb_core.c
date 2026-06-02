#include "gb_core.h"
#include "storage/rom_flash.h"
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdbool.h>

// AUDIO_SAMPLE_RATE=32800 → AUDIO_SAMPLES=549サンプル/フレーム。
// int(32768/59.7275)=548 の切り捨て誤差を補正し、生成レートを 32769Hz に近づける。
// DMA側(TIMER_WRAP=4578→32767Hz)と合わせると GB APU 正規レート 32768Hz との
// 誤差が 0.117% → 0.004% に縮小し、長時間再生でのテンポずれが解消する。
#define AUDIO_SAMPLE_RATE 32800
#define MINIGB_APU_AUDIO_FORMAT_S16SYS
#include "minigb_apu.h"

static struct minigb_apu_ctx apu_ctx;

uint8_t audio_read(uint16_t addr)              { return minigb_apu_audio_read(&apu_ctx, addr); }
void    audio_write(uint16_t addr, uint8_t val){ minigb_apu_audio_write(&apu_ctx, addr, val); }

#define ENABLE_SOUND 1
#define ENABLE_LCD   1
#include "peanut_gb.h"

typedef struct {
    const uint8_t *rom;
    uint8_t *cart_ram;
} rom_ctx_t;

static struct gb_s gb;
static rom_ctx_t  ctx;
static size_t     g_save_size = 0;
static bool       g_cart_dirty = false;

uint8_t gb_fb[GB_SCREEN_H][GB_SCREEN_W];

// ダブルバッファ用: lcd_line_cb が書き込む先を切り替える
static uint8_t (*gb_fb_ptr)[GB_SCREEN_W] = gb_fb;

void gb_core_set_fb(uint8_t (*fb)[GB_SCREEN_W]) {
    gb_fb_ptr = fb;
}

static uint8_t rom_read_cb(struct gb_s *g, const uint_fast32_t addr)
{
    return ((const rom_ctx_t *)g->direct.priv)->rom[addr];
}

static uint8_t cart_ram_read_cb(struct gb_s *g, const uint_fast32_t addr)
{
    const rom_ctx_t *r = g->direct.priv;
    return r->cart_ram ? r->cart_ram[addr] : 0xFF;
}

static void cart_ram_write_cb(struct gb_s *g, const uint_fast32_t addr,
                               const uint8_t val)
{
    rom_ctx_t *r = g->direct.priv;
    if (r->cart_ram) {
        r->cart_ram[addr] = val;
        g_cart_dirty = true;
    }
}

static void error_cb(struct gb_s *g, const enum gb_error_e err,
                     const uint16_t val)
{
    (void)g; (void)err; (void)val;
}

static void lcd_line_cb(struct gb_s *g, const uint8_t *pixels,
                        const uint_fast8_t line)
{
    (void)g;
    if (line < GB_SCREEN_H)
        memcpy(gb_fb_ptr[line], pixels, GB_SCREEN_W);
}

int gb_core_init(void)
{
    ctx.rom      = rom_flash_ptr();
    ctx.cart_ram = NULL;

    enum gb_init_error_e err = gb_init(&gb,
        rom_read_cb, cart_ram_read_cb, cart_ram_write_cb,
        error_cb, &ctx);
    if (err != GB_INIT_NO_ERROR) return -(int)err - 10;

    gb_init_lcd(&gb, lcd_line_cb);

    gb_get_save_size_s(&gb, &g_save_size);
    if (g_save_size > 0) {
        ctx.cart_ram = malloc(g_save_size);
        if (!ctx.cart_ram) return -3;
        memset(ctx.cart_ram, 0xFF, g_save_size);
    }

    minigb_apu_audio_init(&apu_ctx);

    return 0;
}

void gb_core_fill_audio(int16_t *buf)
{
    minigb_apu_audio_callback(&apu_ctx, buf);
}

size_t gb_core_save_size(void)       { return g_save_size; }
uint8_t *gb_core_cart_ram_ptr(void)  { return ctx.cart_ram; }
bool gb_core_is_dirty(void)          { return g_cart_dirty; }
void gb_core_clear_dirty(void)       { g_cart_dirty = false; }
bool gb_core_consume_dirty(void)     { bool d = g_cart_dirty; g_cart_dirty = false; return d; }

void gb_core_run_frame(void)
{
    gb_run_frame(&gb);
}

size_t gb_core_state_size(void)
{
    return sizeof(gb) + sizeof(apu_ctx);
}

void gb_core_state_save(void *buf)
{
    uint8_t *p = (uint8_t *)buf;
    memcpy(p,              &gb,      sizeof(gb));
    memcpy(p + sizeof(gb), &apu_ctx, sizeof(apu_ctx));
}

void gb_core_state_load(const void *buf)
{
    const uint8_t *p = (const uint8_t *)buf;
    memcpy(&gb,      p,              sizeof(gb));
    memcpy(&apu_ctx, p + sizeof(gb), sizeof(apu_ctx));

    // ポインタ類をこのバイナリのアドレスに上書き修正
    gb.gb_rom_read         = rom_read_cb;
    gb.gb_cart_ram_read    = cart_ram_read_cb;
    gb.gb_cart_ram_write   = cart_ram_write_cb;
    gb.gb_error            = error_cb;
    gb.gb_serial_tx        = NULL;
    gb.gb_serial_rx        = NULL;
    gb.gb_bootrom_read     = NULL;
    gb.display.lcd_draw_line = lcd_line_cb;
    gb.direct.priv         = &ctx;
}

void gb_core_set_joypad(uint8_t joypad)
{
    gb.direct.joypad = joypad;
}

void gb_core_reset(void)
{
    gb_reset(&gb);
    minigb_apu_audio_init(&apu_ctx);
}
