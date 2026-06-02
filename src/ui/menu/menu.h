#pragma once
#include <stdint.h>
#include <stdbool.h>

// メニューから main.c に返すアクションコード。
// Flash/SD 操作は main.c 側でロックアウト付きで実行する。
typedef enum {
    MENU_ACT_NONE = 0,
    MENU_ACT_CLOSE,            // B/ESC でメニュー終了（設定は main.c 側で保存）
    MENU_ACT_SRAM_TO_SD,       // 現在の cart RAM を SD にバックアップ
    MENU_ACT_SD_TO_FLASH,      // SD から cart RAM をロードして Flash に保存
    MENU_ACT_FLASH_CLEAR_EXEC, // フラッシュ全消去を実行（確認済み）
} menu_action_t;

// メニューを開いてオーバーレイを描画する。
// fb: ポーズ時のゲームフレームバッファ（パレットプレビュー用）
// LCD mutex 取得済みで呼ぶこと。
void menu_open(const uint8_t fb[144][160],
               uint8_t palette, uint8_t audio_en, uint8_t backlight);

bool menu_is_open(void);

// キー入力を処理して LCD を更新する。LCD mutex 取得済みで呼ぶこと。
// Flash/SD 操作が必要な場合は MENU_ACT_* を返す（実行は呼び出し元の責務）。
menu_action_t menu_tick(int key);

// 操作結果トーストをメニュー上に表示する。LCD mutex 取得済みで呼ぶこと。
// 任意のキー入力で閉じてメニューに戻る。
void menu_show_toast(const char *msg);

// メニュー終了後に main.c が参照する設定値
uint8_t menu_get_palette(void);
uint8_t menu_get_audio_enabled(void);
uint8_t menu_get_backlight(void);
