#pragma once
#include <stdint.h>

// DMA 補充コールバック型。
// DMA IRQ から呼ばれる（Core 0 の ISR コンテキスト）。
// dst に n_samples 個の uint32_t を書くこと。
// フォーマット: bits[15:0] = L チャンネル 10bit PWM、bits[31:16] = R チャンネル 10bit PWM
typedef void (*audio_fill_cb_t)(uint32_t *dst, int n_samples);

// GP26（L）/ GP27（R）を PWM+DMA で初期化する。Core 0 から呼ぶこと（DMA IRQ を Core 0 に登録）。
void audio_pwm_init(void);

// DMA 補充コールバックを登録する。audio_pwm_init() より前に呼ぶこと。
// ISR から呼ばれるため、コールバック内でブロッキング操作をしないこと。
void audio_pwm_set_fill_cb(audio_fill_cb_t cb);

// DMA IRQ と PWM を停止する。dormant 前（multicore_reset_core1() 後）に呼ぶこと。
void audio_pwm_stop(void);
