#include "drivers/audio/audio_pwm.h"
#include "pico.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/dma.h"
#include "hardware/irq.h"

// GP26 = PWM slice 5A (L), GP27 = PWM slice 5B (R)
#define AUDIO_PWM_SLICE  5
#define AUDIO_PIN_L      26
#define AUDIO_PIN_R      27

// slice 4: DMA ペーサー専用（GPIO 出力なし）
// RP2350 デフォルト 150MHz: wrap=4578 → 150MHz/4579 ≈ 32767Hz ≈ GB APU 正規レート（32768Hz）
// wrap=4582（旧値）では 32730Hz となり GB 正規レートより 0.117% 遅かった。
// wrap=4578 では誤差 0.004% まで縮小し、長時間再生でのテンポずれが解消する。
#define TIMER_PWM_SLICE  4
#define TIMER_WRAP       4578

// 12bit 解像度 → キャリア 150MHz/4096 ≈ 36.6kHz（可聴域外）
#define AUDIO_WRAP       4095

// 549 サンプル ≈ 16.8ms/バッファ（GB_AUDIO_SAMPLES と一致させること）
// 548→549 に増やすことで生成レートを 32730Hz→32769Hz に引き上げ、
// DMA レート 32767Hz との差を 39Hz → 2Hz に縮小（FIFO は ~13分で満杯、以降は微量ドロップ）
#define DMA_BUF_SAMPLES  549

static uint32_t     dma_buf[2][DMA_BUF_SAMPLES];
static int          dma_chan  = -1;
static volatile int dma_play  = 0;   // 現在再生中のバッファ番号

static audio_fill_cb_t fill_cb = NULL;

void audio_pwm_set_fill_cb(audio_fill_cb_t cb) {
    fill_cb = cb;
}

// DMA 完了 IRQ: Core 0 に登録（multicore_lockout 中も動作可能）
// SRAM 配置必須: Flash 書き込み中は XIP が無効になるためコードを SRAM に置く
// シングルチャンネル: Flash 書き込み中は割り込み禁止により DMA が停止し短い無音になるが、
// ハードウェアチェーンのように TRANS_COUNT=0 で暴走して「ザザッ」になることはない。
// FatFS が DMA_IRQ_1 を使うため DMA_IRQ_0 を使用。
static void __not_in_flash_func(audio_dma_irq)(void) {
    if (!dma_channel_get_irq0_status(dma_chan)) return;
    dma_channel_acknowledge_irq0(dma_chan);

    // 再生バッファを切り替えて即座に次バッファを DMA 開始
    dma_play ^= 1;
    dma_channel_set_read_addr(dma_chan, dma_buf[dma_play], false);
    dma_channel_set_trans_count(dma_chan, DMA_BUF_SAMPLES, true);  // trigger=true で即座に開始

    // 再生が終わったバッファをリングバッファから補充
    int fill = 1 - dma_play;
    if (fill_cb) fill_cb(dma_buf[fill], DMA_BUF_SAMPLES);
}

// Core 0 から呼ぶこと（DMA IRQ を Core 0 に登録する）
void audio_pwm_init(void)
{
    // PWM slice 5: 音声出力（12bit）
    gpio_set_function(AUDIO_PIN_L, GPIO_FUNC_PWM);
    gpio_set_function(AUDIO_PIN_R, GPIO_FUNC_PWM);
    pwm_config acfg = pwm_get_default_config();
    pwm_config_set_wrap(&acfg, AUDIO_WRAP);
    pwm_init(AUDIO_PWM_SLICE, &acfg, true);
    pwm_set_both_levels(AUDIO_PWM_SLICE, AUDIO_WRAP / 2, AUDIO_WRAP / 2);

    // PWM slice 4: DMA ペーサー（GPIO 使用なし、DREQ のみ利用）
    pwm_config tcfg = pwm_get_default_config();
    pwm_config_set_wrap(&tcfg, TIMER_WRAP);
    pwm_init(TIMER_PWM_SLICE, &tcfg, true);

    // 無音バッファで初期化
    uint32_t silence = ((uint32_t)(AUDIO_WRAP / 2) << 16) | (AUDIO_WRAP / 2);
    for (int i = 0; i < DMA_BUF_SAMPLES; i++) {
        dma_buf[0][i] = silence;
        dma_buf[1][i] = silence;
    }

    // DMA: シングルチャンネル、IRQ で手動再起動
    dma_chan = dma_claim_unused_channel(true);
    dma_channel_config dc = dma_channel_get_default_config(dma_chan);
    channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
    channel_config_set_read_increment(&dc, true);
    channel_config_set_write_increment(&dc, false);
    channel_config_set_dreq(&dc, pwm_get_dreq(TIMER_PWM_SLICE));
    // chain_to はデフォルトで自分自身（チェーンなし = 完了で停止）

    dma_channel_configure(dma_chan, &dc,
        &pwm_hw->slice[AUDIO_PWM_SLICE].cc,
        dma_buf[dma_play],
        DMA_BUF_SAMPLES,
        false);

    dma_channel_set_irq0_enabled(dma_chan, true);

    // DMA_IRQ_0 を Core 0 に登録（Flash 書き込み中も Core 0 IRQ は動作可能）
    irq_set_exclusive_handler(DMA_IRQ_0, audio_dma_irq);
    irq_set_enabled(DMA_IRQ_0, true);

    dma_channel_start(dma_chan);
}

void audio_pwm_stop(void) {
    if (dma_chan < 0) return;
    dma_channel_set_irq0_enabled(dma_chan, false);
    dma_channel_abort(dma_chan);
    pwm_set_enabled(AUDIO_PWM_SLICE, false);
    pwm_set_enabled(TIMER_PWM_SLICE, false);
}
