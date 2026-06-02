#ifndef VIDEO_LCD_H
#define VIDEO_LCD_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// LCD を初期化する。main() の早い段階で一度だけ呼ぶこと。
void lcd_init(void);

// 画面全体を黒で塗りつぶし、カーソルを (0,0) にリセットする。
void lcd_clear(void);

// 起動メッセージなどを緑色で表示する（カーソル位置から連続出力）。
void lcd_print_string(const char *s);

// GB 画面（160×144）を 2x スケールで LCD に差分描画する。
// 前フレームから変化した行のみ転送するため、静的シーンほど高速。
void lcd_gb_frame_delta(const uint8_t fb[144][160]);

// 差分バッファを無効化して次フレームで全 GB 画面を強制再描画する。
// セーブステートロード後に呼ぶこと。
void lcd_gb_frame_invalidate(void);

// 下部 16px ストリップ (y=304-319) にキーヒントを描画する（起動時に一度だけ呼ぶ）。
void lcd_status_draw_hints(void);
// 上部 16px ストリップ中央 (x=52..281) に F1-F4 キーバインドを描画する（起動時に一度だけ呼ぶ）。
void lcd_status_draw_fkey_hints(void);

// 上部 16px ストリップ右端にストレージアクセスインジケーターを表示する。
// 白=読み込み中（SD/Flash 共通）、青=SD 書き込み中、黄=Flash 書き込み中、0=消灯
typedef enum {
    STORAGE_ICON_OFF   = 0,
    STORAGE_ICON_READ  = 1,  // 白: SD / Flash 読み込み
    STORAGE_ICON_SD    = 2,  // 青: SD 書き込み
    STORAGE_ICON_FLASH = 3,  // 黄: Flash 書き込み
} storage_icon_t;

void lcd_status_storage_icon(storage_icon_t state);

// 上部 16px ストリップ左側に最大 8 文字のメッセージを表示する。
void lcd_status_top_text(const char *msg);

// 上部 16px ストリップ右側にバッテリー残量を表示する。pct: 0-100 = 残量%、-1 = 不明
void lcd_status_battery(int pct);

// ── パレット ─────────────────────────────────────────────────────────────────
int         lcd_palette_count(void);
int         lcd_palette_get(void);
const char *lcd_palette_name(int idx);
// パレットを切り替える。dmg_pal565 を更新し prev_fb を無効化する。
void        lcd_palette_set(int idx);

// 現在のパレットでフレームバッファを強制全画面再描画する（メニュープレビュー用）。
void lcd_gb_frame_redraw(const uint8_t fb[144][160]);

// ── メニューオーバーレイ ─────────────────────────────────────────────────────
// items: ラベル文字列の配列（ASCII）、n: 項目数、cursor: ハイライト行
void lcd_menu_draw(const char *const items[], int n, int cursor);
// 単一項目を再描画する（カーソル移動・値変更時の部分更新用）
void lcd_menu_item_redraw(const char *label, int idx, bool selected);
// フラッシュクリア確認ダイアログをメニューの上に描画する
void lcd_menu_draw_confirm(void);
// SD から Flash へ復元する確認ダイアログをメニューの上に描画する
void lcd_menu_draw_sd_confirm(void);
// 操作結果トーストをメニューの上に描画する（任意キーで閉じる旨を表示）
void lcd_menu_draw_toast(const char *msg);

#ifdef __cplusplus
}
#endif

#endif // VIDEO_LCD_H
