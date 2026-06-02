#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define GB_SCREEN_W 160
#define GB_SCREEN_H 144

// ジョイパッドビット定数（active-low: 0=押下, 1=未押下）
#define JOYPAD_A      0x01
#define JOYPAD_B      0x02
#define JOYPAD_SELECT 0x04
#define JOYPAD_START  0x08
#define JOYPAD_RIGHT  0x10
#define JOYPAD_LEFT   0x20
#define JOYPAD_UP     0x40
#define JOYPAD_DOWN   0x80

// DMG パレットインデックス (0-3) のフレームバッファ。
// gb_core_run_frame() 呼び出し後に更新される。
extern uint8_t gb_fb[GB_SCREEN_H][GB_SCREEN_W];

// フラッシュ上の ROM を使ってエミュレータを初期化する。
// rom_flash_ensure() を先に呼ぶこと。
// 戻り値: 0 = 成功、負値 = エラー
int  gb_core_init(void);

// 1 フレーム分エミュレートして gb_fb を更新する。
void gb_core_run_frame(void);

// ジョイパッドの状態をセットする。
// JOYPAD_* 定数のビット OR で指定。0 = 押下、1 = 未押下。
// 初期値はすべて未押下 (0xFF)。
void gb_core_set_joypad(uint8_t joypad);

// lcd_line_cb が書き込む先のフレームバッファを切り替える（ダブルバッファ用）。
void gb_core_set_fb(uint8_t (*fb)[GB_SCREEN_W]);

// セーブ RAM アクセス
size_t   gb_core_save_size(void);
uint8_t *gb_core_cart_ram_ptr(void);
bool     gb_core_is_dirty(void);
void     gb_core_clear_dirty(void);
bool     gb_core_consume_dirty(void); // dirty フラグを返してクリア（フレームごとの書き込み検出用）

// 1フレーム分の S16 ステレオインターリーブ音声サンプルを buf へ書き込む。
// buf は GB_AUDIO_SAMPLES_TOTAL 要素以上の int16_t 配列であること。
#define GB_AUDIO_SAMPLES      549   // L/R ペア数/フレーム（32769Hz / ~59.73fps、AUDIO_SAMPLE_RATE=32800で生成）
#define GB_AUDIO_SAMPLES_TOTAL (GB_AUDIO_SAMPLES * 2)
void gb_core_fill_audio(int16_t *buf);

// GB をソフトリセットする（ROM 再読み込みなし。SRAM は保持）。
void gb_core_reset(void);

// セーブステート: エミュレータ全体の状態（CPU・メモリ・APU）を直列化する。
// buf は gb_core_state_size() バイト以上のバッファであること。
size_t gb_core_state_size(void);
void   gb_core_state_save(void *buf);
void   gb_core_state_load(const void *buf);
