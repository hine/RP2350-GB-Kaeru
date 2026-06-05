#pragma once
#include <stdint.h>
#include <stdbool.h>

// Flash メタデータセクタ（ステートセーブ 10 スロット直後: 1920KB）
//
// Flash レイアウト全体:
//   0x000000 -   1MB : ファームウェア
//   0x100000 - +512KB: ROM データ
//   0x180000 -  +32KB: SRAM セーブ
//   0x188000 - +320KB: セーブステート (10スロット, 各 32KB)
//   0x1D8000 -  +32KB: 未使用（予約）
//   0x1E0000 -   +4KB: メタデータ ← このファイル
//
#define FLASH_META_OFFSET  (1920u * 1024u)

// ── ROM メタデータ ─────────────────────────────────────────────────────────
// ROM が Flash に有効に書き込まれているかを返す（SD なしで起動可能かの判定に使う）
bool flash_meta_rom_valid(void);

// ROM フラッシュ後に呼ぶ。rom_title_11 は GB ヘッダ 0x0134 から 11 バイト。
// Core 1 未起動またはロックアウト済みであること。
void flash_meta_set_rom(const uint8_t *rom_title_11);

// ROM メタデータをクリアする（次回起動時に SD からの再ロードを強制する）。
// メニュー UI から「ROM リロード」操作に使う。
// Core 1 ロックアウト済みであること。
void flash_meta_clear_rom(void);

// ── ユーザー設定 ──────────────────────────────────────────────────────────────
// デフォルト: palette_idx=0(DMGGreen), audio_en=1(ON), backlight=128
// Core 1 未起動または任意のタイミングで呼び出し可（XIP 読み取りのみ）。
void flash_settings_load(uint8_t *palette_idx, uint8_t *audio_en, uint8_t *backlight);

// 設定を Flash に書き込む。Core 1 ロックアウト済みであること。
void flash_settings_save(uint8_t palette_idx, uint8_t audio_en, uint8_t backlight);

// メタデータセクタ全消去（ROM/SRAM フラグ・全スロットフラグ・設定をすべてクリア）。
// 次回起動時に SD からの全データ再ロードを強制し、全スロットが空として扱われる。
// Core 1 ロックアウト済みであること。
void flash_meta_clear_all(void);

// ── セーブステートメタデータ ───────────────────────────────────────────────
// slot が有効なデータを持つかを返す。
bool flash_meta_state_valid(int slot);

// セーブステート書き込み後に呼ぶ。slot を有効としてフラグを立てる。
// Core 1 ロックアウト済みであること。
void flash_meta_set_state(int slot);

// ── SRAM セーブメタデータ ──────────────────────────────────────────────────
// SRAM セーブが有効かつ rom_title_11 が一致するかを返す。
// 異なる ROM のセーブデータが読み込まれるクロス汚染を防ぐ。
bool flash_meta_sram_valid(const uint8_t *rom_title_11);

// SRAM セーブ後に呼ぶ。Core 1 ロックアウト済みであること。
void flash_meta_set_sram(const uint8_t *rom_title_11);

// SRAM セーブをクリアする。メニュー UI から「セーブデータ削除」に使う。
// Core 1 ロックアウト済みであること。
void flash_meta_clear_sram(void);

// ── 完全消去 ─────────────────────────────────────────────────────────────────
// ROM / SRAM セーブ / セーブステート全スロット / メタデータを物理消去する（約 2.5 秒）。
// 完了後はデバイスのリブートが必要。Core 1 ロックアウト済みであること。
void flash_full_erase(void);
