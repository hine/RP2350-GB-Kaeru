/**
 * Game Boy APU emulator.
 * Copyright (c) 2019 Mahyar Koshkouei <mk@deltabeard.com>
 * Copyright (c) 2017 Alex Baines <alex@abaines.me.uk>
 * minigb_apu is released under the terms of the MIT license.
 *
 * minigb_apu emulates the audio processing unit (APU) of the Game Boy. This
 * project is based on MiniGBS by Alex Baines: https://github.com/baines/MiniGBS
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "minigb_apu.h"

#define DMG_CLOCK_FREQ_U	((unsigned)DMG_CLOCK_FREQ)
#define AUDIO_NSAMPLES		(AUDIO_SAMPLES_TOTAL)

#define MAX(a, b)		( a > b ? a : b )
#define MIN(a, b)		( a <= b ? a : b )

/* Factor in which values are multiplied to compensate for fixed-point
 * arithmetic. Some hard-coded values in this project must be recreated. */
#ifndef FREQ_INC_MULT
# define FREQ_INC_MULT		105
#endif
/* Handles time keeping for sound generation.
 * FREQ_INC_REF must be equal to, or larger than AUDIO_SAMPLE_RATE in order
 * to avoid a division by zero error.
 * Using a square of 2 simplifies calculations. */
#define FREQ_INC_REF		(AUDIO_SAMPLE_RATE * FREQ_INC_MULT)

#define MAX_CHAN_VOLUME		15

static void set_note_freq(struct chan *c)
{
	/* Lowest expected value of freq is 64. */
	uint32_t freq = (DMG_CLOCK_FREQ_U / 4) / (2048 - c->freq);
	c->freq_inc = freq * (uint32_t)(FREQ_INC_REF / AUDIO_SAMPLE_RATE);
}

static void chan_enable(struct minigb_apu_ctx *ctx,
		const uint_fast8_t i, const bool enable)
{
	uint8_t val;

	ctx->chans[i].enabled = enable;
	val = (ctx->audio_mem[0xFF26 - AUDIO_ADDR_COMPENSATION] & 0x80) |
		(ctx->chans[3].enabled << 3) | (ctx->chans[2].enabled << 2) |
		(ctx->chans[1].enabled << 1) | (ctx->chans[0].enabled << 0);

	ctx->audio_mem[0xFF26 - AUDIO_ADDR_COMPENSATION] = val;
}

/* ── Frame Sequencer Tick Functions (discrete, called at exact rates) ────── */

/* Called at 256 Hz (steps 0,2,4,6). */
static void tick_len(struct minigb_apu_ctx *ctx, uint_fast8_t ci)
{
	struct chan *c = &ctx->chans[ci];
	if (!c->len.enabled || c->len.ctr == 0)
		return;
	if (--c->len.ctr == 0)
		chan_enable(ctx, ci, 0);
}

/* Called at 64 Hz (step 7). */
static void tick_env(struct chan *c)
{
	if (c->env.step == 0)
		return;
	if (--c->env.ctr == 0) {
		c->env.ctr = c->env.step;
		if (c->env.up) {
			if (c->volume < MAX_CHAN_VOLUME) c->volume++;
		} else {
			if (c->volume > 0) c->volume--;
		}
	}
}

/* Called at 128 Hz (steps 2,6) — CH1 only. */
static void tick_sweep(struct minigb_apu_ctx *ctx)
{
	struct chan *c = &ctx->chans[0];
	if (c->sweep.rate == 0)
		return;
	if (--c->sweep.ctr == 0) {
		c->sweep.ctr = c->sweep.rate;
		if (c->sweep.shift != 0) {
			uint16_t delta    = c->sweep.freq >> c->sweep.shift;
			uint16_t new_freq = c->sweep.down
				? c->sweep.freq - delta
				: c->sweep.freq + delta;
			if (new_freq > 2047) {
				chan_enable(ctx, 0, 0);
			} else {
				c->sweep.freq = new_freq;
				c->freq       = new_freq;
				set_note_freq(c);
			}
		}
	}
}

/* Advance the 512 Hz frame sequencer by one step.
 * Step schedule (Pan Docs):
 *   0: Length   2: Length+Sweep   4: Length   6: Length+Sweep
 *   1,3,5: -    7: Envelope
 */
static void tick_frame_sequencer(struct minigb_apu_ctx *ctx)
{
	uint8_t step = ctx->frame_seq_step;

	/* Length counter: steps 0,2,4,6  (256 Hz) */
	if ((step & 1) == 0) {
		for (uint_fast8_t i = 0; i < 4; i++)
			tick_len(ctx, i);
	}

	/* Sweep: steps 2,6  (128 Hz — (step & 3) == 2 catches both 010 and 110) */
	if ((step & 3) == 2)
		tick_sweep(ctx);

	/* Envelope: step 7  (64 Hz) */
	if (step == 7) {
		for (uint_fast8_t i = 0; i < 4; i++)
			tick_env(&ctx->chans[i]);
	}

	ctx->frame_seq_step = (step + 1) & 7;
}

static bool update_freq(struct chan *c, uint32_t *pos)
{
	uint32_t inc = c->freq_inc - *pos;
	c->freq_counter += inc;

	if (c->freq_counter > FREQ_INC_REF) {
		*pos		= c->freq_inc - (c->freq_counter - FREQ_INC_REF);
		c->freq_counter = 0;
		return true;
	} else {
		*pos = c->freq_inc;
		return false;
	}
}


static void update_square(struct minigb_apu_ctx *ctx, audio_sample_t *samples,
		const bool ch2, uint_fast16_t offset, uint_fast16_t count)
{
	struct chan *c = &ctx->chans[ch2];

	if (!c->powered || !c->enabled)
		return;

	set_note_freq(c);

	for (uint_fast16_t i = offset; i < offset + count; i += 2) {
		if (!c->enabled)
			return;
		if (!c->volume)
			continue;

		uint32_t pos = 0;
		uint32_t prev_pos = 0;
		int32_t sample = 0;

		while (update_freq(c, &pos)) {
			c->square.duty_counter = (c->square.duty_counter + 1) & 7;
			sample += ((pos - prev_pos) / c->freq_inc) * c->val;
			c->val = (c->square.duty & (1 << c->square.duty_counter)) ?
				VOL_INIT_MAX / MAX_CHAN_VOLUME :
				VOL_INIT_MIN / MAX_CHAN_VOLUME;
			prev_pos = pos;
		}

		sample += c->val;
		sample *= c->volume;
		sample /= 4;

		samples[i + 0] += sample * c->on_left * ctx->vol_l;
		samples[i + 1] += sample * c->on_right * ctx->vol_r;
	}
}

static uint8_t wave_sample(struct minigb_apu_ctx *ctx,
		const unsigned int pos, const unsigned int volume)
{
	uint8_t sample;

	sample =  ctx->audio_mem[(0xFF30 + pos / 2) - AUDIO_ADDR_COMPENSATION];
	if (pos & 1) {
		sample &= 0xF;
	} else {
		sample >>= 4;
	}
	return volume ? (sample >> (volume - 1)) : 0;
}

static void update_wave(struct minigb_apu_ctx *ctx, audio_sample_t *samples,
		uint_fast16_t offset, uint_fast16_t count)
{
	struct chan *c = &ctx->chans[2];

	if (!c->powered || !c->enabled || !c->volume)
		return;

	set_note_freq(c);
	c->freq_inc *= 2;

	for (uint_fast16_t i = offset; i < offset + count; i += 2) {
		if (!c->enabled)
			return;

		uint32_t pos = 0;
		uint32_t prev_pos = 0;
		audio_sample_t sample = 0;

		c->wave.sample = wave_sample(ctx, c->val, c->volume);

		while (update_freq(c, &pos)) {
			c->val = (c->val + 1) & 31;
			sample += ((pos - prev_pos) / c->freq_inc) *
				((audio_sample_t)c->wave.sample - 8) *
					(AUDIO_SAMPLE_MAX/64);
			c->wave.sample = wave_sample(ctx, c->val, c->volume);
			prev_pos  = pos;
		}

		sample += ((audio_sample_t)c->wave.sample - 8) *
				(audio_sample_t)(AUDIO_SAMPLE_MAX/64);
		{
			/* First element is unused. */
			audio_sample_t div[] = { AUDIO_SAMPLE_MAX, 1, 2, 4 };
			sample = sample / (div[c->volume]);
		}

		sample /= 4;
		samples[i + 0] += sample * c->on_left * ctx->vol_l;
		samples[i + 1] += sample * c->on_right * ctx->vol_r;
	}
}

static void update_noise(struct minigb_apu_ctx *ctx, audio_sample_t *samples,
		uint_fast16_t offset, uint_fast16_t count)
{
	struct chan *c = &ctx->chans[3];

	if (c->freq >= 14)
		c->enabled = 0;

	if (!c->powered || !c->enabled)
		return;

	{
		const uint32_t lfsr_div_lut[] = {
			8, 16, 32, 48, 64, 80, 96, 112
		};
		uint32_t freq;

		freq = DMG_CLOCK_FREQ_U / (lfsr_div_lut[c->noise.lfsr_div] << c->freq);
		c->freq_inc = freq * (uint32_t)(FREQ_INC_REF / AUDIO_SAMPLE_RATE);
	}

	for (uint_fast16_t i = offset; i < offset + count; i += 2) {
		if (!c->enabled)
			return;
		if (!c->volume)
			continue;

		uint32_t pos      = 0;
		uint32_t prev_pos = 0;
		int32_t sample    = 0;

		while (update_freq(c, &pos)) {
			/* Pan Docs: XOR bits 1 and 0, shift right, insert at bit 14.
			 * In 7-bit mode also insert at bit 6.
			 * Output is HIGH when bit 0 is clear. */
			uint16_t xor_bit = (c->noise.lfsr_reg ^ (c->noise.lfsr_reg >> 1)) & 1;
			c->noise.lfsr_reg = (c->noise.lfsr_reg >> 1) | (xor_bit << 14);
			if (!c->noise.lfsr_wide)
				c->noise.lfsr_reg |= (xor_bit << 6);

			c->val = (c->noise.lfsr_reg & 1) ?
				VOL_INIT_MIN / MAX_CHAN_VOLUME :
				VOL_INIT_MAX / MAX_CHAN_VOLUME;

			sample += ((pos - prev_pos) / c->freq_inc) * c->val;
			prev_pos = pos;
		}

		sample += c->val;
		sample *= c->volume;
		sample /= 4;

		samples[i + 0] += sample * c->on_left * ctx->vol_l;
		samples[i + 1] += sample * c->on_right * ctx->vol_r;
	}
}

/**
 * SDL2 style audio callback function.
 */
void minigb_apu_audio_callback(struct minigb_apu_ctx *ctx,
		audio_sample_t *stream)
{
	memset(stream, 0, AUDIO_SAMPLES_TOTAL * sizeof(audio_sample_t));

	/* Process the buffer in chunks of CHUNK_SAMPLES so the frame sequencer
	 * fires between chunks.  At 32768 Hz the sequencer fires every 64
	 * samples (512 Hz), so one tick fires at the end of each chunk.
	 * Short sounds (coins, battle SFX) whose length counters expire
	 * mid-buffer are silenced only for the remaining chunks, not the whole
	 * buffer. */
#define CHUNK_SAMPLES 64u
	for (uint_fast16_t offset = 0; offset < AUDIO_NSAMPLES; offset += CHUNK_SAMPLES) {
		uint_fast16_t n = CHUNK_SAMPLES;
		if (offset + n > AUDIO_NSAMPLES)
			n = AUDIO_NSAMPLES - offset;

		update_square(ctx, stream, 0, offset, n);
		update_square(ctx, stream, 1, offset, n);
		update_wave(ctx, stream, offset, n);
		update_noise(ctx, stream, offset, n);

		/* 512 counts per mono sample; fire when >= AUDIO_SAMPLE_RATE. */
		ctx->frame_seq_count += 512u * n;
		while (ctx->frame_seq_count >= (uint32_t)AUDIO_SAMPLE_RATE) {
			ctx->frame_seq_count -= (uint32_t)AUDIO_SAMPLE_RATE;
			tick_frame_sequencer(ctx);
		}
	}
#undef CHUNK_SAMPLES
}

static void chan_trigger(struct minigb_apu_ctx *ctx, uint_fast8_t i)
{
	struct chan *c = &ctx->chans[i];

	chan_enable(ctx, i, 1);
	c->volume = c->volume_init;

	// Volume envelope: load period counter (if step=0 load 8 per Pan Docs)
	{
		uint8_t val = ctx->audio_mem[(0xFF12 + (i * 5)) - AUDIO_ADDR_COMPENSATION];
		c->env.step = val & 0x7;
		c->env.up   = (val >> 3) & 1;
		c->env.ctr  = c->env.step ? c->env.step : 8;
	}

	// Frequency sweep (CH1 only): load shadow freq and period counter
	if (i == 0) {
		uint8_t val = ctx->audio_mem[0xFF10 - AUDIO_ADDR_COMPENSATION];
		c->sweep.freq  = c->freq;
		c->sweep.rate  = (val >> 4) & 0x07;
		c->sweep.down  = (val & 0x08) != 0;
		c->sweep.shift = (val & 0x07);
		c->sweep.ctr   = c->sweep.rate ? c->sweep.rate : 8;
	}

	// Length counter: per Pan Docs, reload with max if currently 0
	int len_max = 64;
	if (i == 2) { // wave
		len_max = 256;
		c->val = 0;
	} else if (i == 3) { // noise
		c->noise.lfsr_reg = 0xFFFF;
		c->val = VOL_INIT_MIN / MAX_CHAN_VOLUME;
	}
	if (c->len.ctr == 0)
		c->len.ctr = (uint16_t)len_max;
}

/**
 * Read audio register.
 * \param addr	Address of audio register. Must be 0xFF10 <= addr <= 0xFF3F.
 *				This is not checked in this function.
 * \return	Byte at address.
 */
uint8_t minigb_apu_audio_read(struct minigb_apu_ctx *ctx, const uint16_t addr)
{
	static const uint8_t ortab[] = {
		0x80, 0x3f, 0x00, 0xff, 0xbf,
		0xff, 0x3f, 0x00, 0xff, 0xbf,
		0x7f, 0xff, 0x9f, 0xff, 0xbf,
		0xff, 0xff, 0x00, 0x00, 0xbf,
		0x00, 0x00, 0x70,
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	};

	return ctx->audio_mem[addr - AUDIO_ADDR_COMPENSATION] |
		ortab[addr - AUDIO_ADDR_COMPENSATION];
}

/**
 * Write audio register.
 * \param addr	Address of audio register. Must be 0xFF10 <= addr <= 0xFF3F.
 *				This is not checked in this function.
 * \param val	Byte to write at address.
 */
void minigb_apu_audio_write(struct minigb_apu_ctx *ctx,
		const uint16_t addr, const uint8_t val)
{
	/* Find sound channel corresponding to register address. */
	uint_fast8_t i;

	if(addr == 0xFF26)
	{
		ctx->audio_mem[addr - AUDIO_ADDR_COMPENSATION] = val & 0x80;
		/* On APU power off, clear all registers apart from wave
		 * RAM. */
		if((val & 0x80) == 0)
		{
			memset(ctx->audio_mem,
					0x00, 0xFF26 - AUDIO_ADDR_COMPENSATION);
			ctx->chans[0].enabled = false;
			ctx->chans[1].enabled = false;
			ctx->chans[2].enabled = false;
			ctx->chans[3].enabled = false;
		}

		return;
	}

	/* Ignore register writes if APU powered off. */
	if(ctx->audio_mem[0xFF26 - AUDIO_ADDR_COMPENSATION] == 0x00)
		return;

	ctx->audio_mem[addr - AUDIO_ADDR_COMPENSATION] = val;
	i = (addr - AUDIO_ADDR_COMPENSATION) / 5;

	switch (addr) {
	case 0xFF12:
	case 0xFF17:
	case 0xFF21: {
		ctx->chans[i].volume_init = val >> 4;
		ctx->chans[i].powered     = (val >> 3) != 0;
		ctx->chans[i].env.step    = val & 0x07;
		ctx->chans[i].env.up      = (val >> 3) & 1;
	} break;

	case 0xFF1C:
		ctx->chans[i].volume = ctx->chans[i].volume_init = (val >> 5) & 0x03;
		break;

	case 0xFF11:
	case 0xFF16:
	case 0xFF20: {
		const uint8_t duty_lookup[] = { 0x10, 0x30, 0x3C, 0xCF };
		ctx->chans[i].len.ctr     = 64 - (val & 0x3f);
		ctx->chans[i].square.duty = duty_lookup[val >> 6];
		break;
	}

	case 0xFF1B:
		ctx->chans[i].len.ctr = 256 - val;
		break;

	case 0xFF13:
	case 0xFF18:
	case 0xFF1D:
		ctx->chans[i].freq &= 0xFF00;
		ctx->chans[i].freq |= val;
		break;

	case 0xFF1A:
		ctx->chans[i].powered = (val & 0x80) != 0;
		chan_enable(ctx, i, val & 0x80);
		break;

	case 0xFF14:
	case 0xFF19:
	case 0xFF1E:
		ctx->chans[i].freq &= 0x00FF;
		ctx->chans[i].freq |= ((val & 0x07) << 8);
		/* Intentional fall-through. */
	case 0xFF23:
		ctx->chans[i].len.enabled = val & 0x40;
		if (val & 0x80)
			chan_trigger(ctx, i);

		break;

	case 0xFF22:
		ctx->chans[3].freq = val >> 4;
		ctx->chans[3].noise.lfsr_wide = !(val & 0x08);
		ctx->chans[3].noise.lfsr_div = val & 0x07;
		break;

	case 0xFF24:
	{
		ctx->vol_l = ((val >> 4) & 0x07);
		ctx->vol_r = (val & 0x07);
		break;
	}

	case 0xFF25:
		for (uint_fast8_t j = 0; j < 4; j++) {
			ctx->chans[j].on_left  = (val >> (4 + j)) & 1;
			ctx->chans[j].on_right = (val >> j) & 1;
		}
		break;
	}
}

void minigb_apu_audio_init(struct minigb_apu_ctx *ctx)
{
	/* Initialise channels and samples. */
	memset(ctx->chans, 0, sizeof(ctx->chans));
	ctx->chans[0].val = ctx->chans[1].val = -1;

	/* Initialise frame sequencer. */
	ctx->frame_seq_count = 0;
	ctx->frame_seq_step  = 0;

	/* Initialise IO registers. */
	{
		const uint8_t regs_init[] = { 0x80, 0xBF, 0xF3, 0xFF, 0x3F,
					      0xFF, 0x3F, 0x00, 0xFF, 0x3F,
					      0x7F, 0xFF, 0x9F, 0xFF, 0x3F,
					      0xFF, 0xFF, 0x00, 0x00, 0x3F,
					      0x77, 0xF3, 0xF1 };

		for(uint_fast8_t i = 0; i < sizeof(regs_init); ++i)
			minigb_apu_audio_write(ctx, 0xFF10 + i, regs_init[i]);
	}

	/* Initialise Wave Pattern RAM. */
	{
		const uint8_t wave_init[] = { 0xac, 0xdd, 0xda, 0x48,
					      0x36, 0x02, 0xcf, 0x16,
					      0x2c, 0x04, 0xe5, 0x2c,
					      0xac, 0xdd, 0xda, 0x48 };

		for(uint_fast8_t i = 0; i < sizeof(wave_init); ++i)
			minigb_apu_audio_write(ctx, 0xFF30 + i, wave_init[i]);
	}
}
