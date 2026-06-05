#include "drivers/audio/es8311.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include <string.h>

#define ES8311_I2C_TIMEOUT_US 5000  /* 5 ms — prevents hang on stuck I2C bus */

// ── Register map ─────────────────────────────────────────────────────────────
#define REG00_RESET     0x00
#define REG01_CLK1      0x01
#define REG02_CLK2      0x02
#define REG03_CLK3      0x03
#define REG04_CLK4      0x04
#define REG05_CLK5      0x05
#define REG06_CLK6      0x06
#define REG07_CLK7      0x07
#define REG08_CLK8      0x08
#define REG09_SDPIN     0x09
#define REG0A_SDPOUT    0x0A
#define REG0D_SYS       0x0D
#define REG0E_SYS       0x0E
#define REG12_SYS       0x12
#define REG13_SYS       0x13
#define REG1C_ADC       0x1C
#define REG31_DAC       0x31
#define REG32_DACVOL    0x32
#define REG37_DAC       0x37
#define REGFD_ID1       0xFD
#define REGFE_ID2       0xFE

// ── Clock coefficient table ───────────────────────────────────────────────────
// {mclk_hz, sample_hz, pre_div, pre_multi, adc_div, dac_div,
//  fs_mode, lrck_h, lrck_l, bclk_div, adc_osr, dac_osr}
struct coeff_div {
    uint32_t mclk;
    uint32_t rate;
    uint8_t  pre_div;
    uint8_t  pre_multi;
    uint8_t  adc_div;
    uint8_t  dac_div;
    uint8_t  fs_mode;
    uint8_t  lrck_h;
    uint8_t  lrck_l;
    uint8_t  bclk_div;
    uint8_t  adc_osr;
    uint8_t  dac_osr;
};

static const struct coeff_div coeff_table[] = {
    // 8k
    {12288000, 8000, 6,1,1,1,0,0,0xFF,4,16,16},
    {6144000,  8000, 3,0,1,1,0,0,0xFF,4,16,16},
    // 16k
    {12288000,16000,3,0,1,1,0,0,0xFF,4,16,16},
    {6144000, 16000,3,1,1,1,0,0,0xFF,4,16,16},
    // 32k
    {12288000,32000,3,1,1,1,0,0,0xFF,4,16,16},
    {6144000, 32000,3,2,1,1,0,0,0xFF,4,16,16},
    {8192000, 32000,1,0,1,1,0,0,0xFF,4,16,16},
    // 44.1k
    {11289600,44100,1,0,1,1,0,0,0xFF,4,16,16},
    // 48k
    {12288000,48000,1,0,1,1,0,0,0xFF,4,16,16},
    {6144000, 48000,1,1,1,1,0,0,0xFF,4,16,16},
};

// ── I2C helpers ───────────────────────────────────────────────────────────────
/* Use timeout variants: FT3168 and ES8311 share the same I2C bus, so a hung
 * ES8311 transaction would otherwise block the touch reads on Core 0 forever. */
static void wr(i2c_inst_t *i2c, uint8_t reg, uint8_t val) {
    uint8_t buf[2] = {reg, val};
    i2c_write_blocking_until(i2c, ES8311_I2C_ADDR, buf, 2, false,
                             make_timeout_time_us(ES8311_I2C_TIMEOUT_US));
}

static uint8_t rd(i2c_inst_t *i2c, uint8_t reg) {
    uint8_t val = 0;
    int r = i2c_write_blocking_until(i2c, ES8311_I2C_ADDR, &reg, 1, true,
                                     make_timeout_time_us(ES8311_I2C_TIMEOUT_US));
    if (r < 0) return 0;
    i2c_read_blocking_until(i2c, ES8311_I2C_ADDR, &val, 1, false,
                            make_timeout_time_us(ES8311_I2C_TIMEOUT_US));
    return val;
}

// ── Clock config ──────────────────────────────────────────────────────────────
static int find_coeff(uint32_t mclk, uint32_t rate) {
    for (int i = 0; i < (int)(sizeof(coeff_table)/sizeof(coeff_table[0])); i++)
        if (coeff_table[i].mclk == mclk && coeff_table[i].rate == rate)
            return i;
    return -1;
}

static void apply_clock(i2c_inst_t *i2c, uint32_t mclk_hz, uint32_t sample_rate) {
    int idx = find_coeff(mclk_hz, sample_rate);
    if (idx < 0) {
        // No coefficient found — leave clock registers at default
        return;
    }
    const struct coeff_div *c = &coeff_table[idx];

    uint8_t r02 = rd(i2c, REG02_CLK2) & 0x07;
    r02 |= (uint8_t)((c->pre_div - 1) << 5) | (uint8_t)(c->pre_multi << 3);
    wr(i2c, REG02_CLK2, r02);

    wr(i2c, REG03_CLK3, (uint8_t)((c->fs_mode << 6) | c->adc_osr));
    wr(i2c, REG04_CLK4, c->dac_osr);
    wr(i2c, REG05_CLK5, (uint8_t)(((c->adc_div - 1) << 4) | (c->dac_div - 1)));

    uint8_t r06 = rd(i2c, REG06_CLK6) & 0xE0;
    r06 |= (c->bclk_div < 19) ? (uint8_t)(c->bclk_div - 1) : c->bclk_div;
    wr(i2c, REG06_CLK6, r06);

    uint8_t r07 = rd(i2c, REG07_CLK7) & 0xC0;
    r07 |= c->lrck_h;
    wr(i2c, REG07_CLK7, r07);
    wr(i2c, REG08_CLK8, c->lrck_l);
}

// ── Public API ────────────────────────────────────────────────────────────────
void es8311_init(i2c_inst_t *i2c, uint32_t sample_rate, uint32_t mclk_hz) {
    // Reset
    wr(i2c, REG00_RESET, 0x1F);
    sleep_ms(20);
    wr(i2c, REG00_RESET, 0x00);
    wr(i2c, REG00_RESET, 0x80); // power-on

    // Clock source = MCLK pin; enable all clocks
    wr(i2c, REG01_CLK1, 0x3F);

    uint8_t r06 = rd(i2c, REG06_CLK6) & ~(1u << 5); // SCLK not inverted
    r06 |= 0x03;                                       // BCLK master
    wr(i2c, REG06_CLK6, r06);

    apply_clock(i2c, mclk_hz, sample_rate);

    // Serial port: master mode, I2S, 16-bit
    uint8_t r00 = rd(i2c, REG00_RESET) | 0x40; // master
    wr(i2c, REG00_RESET, r00);
    wr(i2c, REG09_SDPIN,  0x0C); // 16-bit I2S in
    wr(i2c, REG0A_SDPOUT, 0x0C); // 16-bit I2S out

    // Power up analog
    wr(i2c, REG0D_SYS, 0x01);
    wr(i2c, REG0E_SYS, 0x02);
    wr(i2c, REG12_SYS, 0x00); // DAC power up
    wr(i2c, REG13_SYS, 0x10); // HP output enable
    wr(i2c, REG1C_ADC, 0x6A); // ADC EQ bypass
    wr(i2c, REG37_DAC, 0x08); // DAC EQ bypass
}

void es8311_set_volume(i2c_inst_t *i2c, int volume) {
    if (volume < 0)   volume = 0;
    if (volume > 100) volume = 100;
    uint8_t reg = (volume == 0) ? 0 : (uint8_t)(volume * 256 / 100 - 1);
    wr(i2c, REG32_DACVOL, reg);
}

void es8311_mute(i2c_inst_t *i2c, bool mute) {
    uint8_t r = rd(i2c, REG31_DAC);
    if (mute) r |=  (1u << 6) | (1u << 5);
    else      r &= ~((1u << 6) | (1u << 5));
    wr(i2c, REG31_DAC, r);
}

uint16_t es8311_read_id(i2c_inst_t *i2c) {
    uint8_t lo = rd(i2c, REGFD_ID1);
    uint8_t hi = rd(i2c, REGFE_ID2);
    return (uint16_t)((hi << 8) | lo);
}
