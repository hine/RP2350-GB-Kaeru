# 移植ガイド — PicoCalc-GB-Kaeru → RP2350-GB-Kaeru

このドキュメントは PicoCalc-GB-Kaeru の開発で得た非自明な知見をまとめたものです。  
フォーク先の新プロジェクト（RP2350-GB-Kaeru）を始める際に、同じ問題で時間を失わないための記録です。

---

## 1. そのまま使える資産

以下のモジュールはターゲット非依存で、ほぼそのまま移植できます。

| モジュール | ファイル | 備考 |
|---|---|---|
| GB エミュレーションコア | `emu/gb/gb_core.c/h` | Peanut-GB ラッパー。ROM XIP 読み込み・APU・セーブステート統合済み |
| SRAM セーブ | `src/storage/save_flash.c` | マジック + ROM タイトル検証ロジックを維持すること（後述） |
| セーブステート | `src/storage/save_flash.c` | Flash レイアウトはターゲットに合わせて調整が必要 |
| Flash メタデータ | `src/storage/flash_meta.c/h` | ROM 有効フラグ・SRAM 有効フラグ |
| ROM Flash 書き込み | `src/storage/rom_flash.c/h` | XIP オフセットはターゲットの Flash レイアウトに合わせる |
| SPSC リングバッファ | `emu/gb/gb_core.c` 内の `g_afifo` | APU → 音声出力の橋渡し。サンプルレートが変わっても構造は使える |
| メニューロジック | `src/menu/menu.c/h` | 表示部分は Display HAL に差し替えが必要 |

---

## 2. 音声アーキテクチャ（最重要）

### 2.1 確定した構成（PicoCalc）

```
Core 0:
  GB エミュレーション
  APU サンプル生成 → g_afifo（SPSC リングバッファ）書き込み
  音声 DMA IRQ（リングバッファ → DMA バッファ → PWM）

Core 1:
  LCD 描画
  キーボードポーリング
  multicore_lockout_victim_init()
```

### 2.2 IRQ を Core 0 に置く理由（重要）

**Core 1 に音声 DMA IRQ を置いてはいけない。**

Flash 書き込み時に `multicore_lockout_start_blocking()` を呼ぶと Core 1 が停止する。  
この停止中にハードウェアチェーン DMA が走ると `TRANS_COUNT=0` のまま暴走し、スピーカーから「ザザッ」という大きなノイズが出る。

Core 0 に IRQ を登録した場合、lockout の影響を受けない。Flash 書き込み中は単に無音になるだけで、暴走は起きない。

### 2.3 IRQ ハンドラの配置

```c
void __not_in_flash_func(audio_dma_irq_handler)(void) { ... }
```

Flash 書き込み中は XIP が無効になる。IRQ ハンドラが Flash 上にあると、その瞬間にハードフォルトする。  
`__not_in_flash_func` で SRAM に配置することが必須。

### 2.4 I2S 化への移行ポイント

PicoCalc の PWM 実装との違い：

| 項目 | PWM（PicoCalc） | I2S（AMOLED） |
|---|---|---|
| 出力先 | GP26/27 直結 PWM | PIO → ES8311 |
| サンプルレート | ~32768Hz（TIMER_WRAP で近似） | 整数割り切れる値（32000Hz or 44100Hz）が使える |
| DMA 構成 | シングルチャンネル + ソフト再チェーン | 基本的に同じパターンが使える |
| IRQ ハンドラ | `__not_in_flash_func` 必須 | 同じく必須 |

SPSC リングバッファ（g_afifo）と DMA IRQ 駆動の骨格はそのまま流用できる。  
出力部分（PWM → PIO/I2S）だけを差し替えるイメージで実装できる。

サンプルレートが整数に揃うため、PicoCalc で苦しんだ**テンポずれ問題が根本解決する可能性がある**。  
（PicoCalc では TIMER_WRAP の int 切り捨てで毎フレーム 0.625 サンプル分が失われ、0.117% 遅れていた）

### 2.5 ハードウェアチェーン DMA は使わない

RP2350 の DMA ハードウェアチェーン機能（`chain_to`）は Flash 書き込みと組み合わせると上記の暴走問題が起きる。  
ソフトウェア再チェーン（IRQ ハンドラで次の転送を手動セットアップ）を使うこと。

---

## 3. マルチコア・Flash 書き込み

### 3.1 lockout の必須性

Core 1 は Flash 上のコード（LovyanGFX 等）を実行している。  
Flash 消去・書き込み中に Core 1 が Flash へアクセスすると即座にハードフォルトする。

```c
// Flash 書き込み前後で必ず囲む
multicore_lockout_start_blocking();
// ... flash_range_erase / flash_range_program ...
multicore_lockout_end_blocking();
```

Core 1 側では起動時に一度だけ：

```c
multicore_lockout_victim_init();
```

### 3.2 g_menu_active フラグ

メニュー表示中に Core 1 がフレーム描画を上書きする競合を防ぐため、`volatile bool g_menu_active` フラグが必要。  
Core 1 の描画ループで「`g_menu_active` が true なら描画をスキップ」というガードを入れること。

---

## 4. セーブデータの検証（重要）

### 4.1 ブランクチェックでは不十分

Flash の SRAM セーブ領域が「0xFF で埋まっているかどうか」だけで判定すると、  
別ビルドや別 ROM のゴミデータが残っている場合に誤ロードしてゲームのセーブが壊れる。

### 4.2 採用した方式

Flash メタデータにマジックバイト + ROM タイトル（11 バイト）を記録し、ロード前に照合する。

```c
#define SRAM_MAGIC 0xCAFEBABE
// メタデータ領域に { magic, rom_title[11] } を保存
// ロード時に両方が一致した場合のみ復元する
```

### 4.3 `is_dirty()` ではなく `consume_dirty()` を使う

Peanut-GB の `gb_is_dirty()` はラッチ型で、一度 true になると次に `gb_is_dirty()` を呼ぶまで true を返し続ける。  
毎フレームのデバウンスカウントダウンループで使うと「カウントが 0 に到達しない」バグになる。

```c
// 誤り: gb_is_dirty() を毎フレーム呼ぶ
// 正しい: 専用関数でラッチをクリア
bool gb_core_consume_dirty(void) {
    if (gb_is_dirty()) {
        (void)gb_is_dirty(); // ラッチクリア
        return true;
    }
    return false;
}
```

---

## 5. ROM の XIP 提供

### 5.1 なぜ XIP か

SD カードからの毎フレーム読み込みは ~7.5fps しか出なかった。  
Flash XIP（`0x10000000 + オフセット`）で直接読むと ~60fps を達成できた。

### 5.2 初回フラッシュ戦略

```
起動時:
  1. Flash メタデータを確認（ROM 有効フラグ）
  2. SD がある場合: ROM タイトル照合 → 差分があれば再フラッシュ
  3. ROM 有効フラグが立っていれば SD なしで起動可能
```

### 5.3 Flash レイアウトの調整

PicoCalc の Flash レイアウト（参考）：

```
0x000000  1MB   ファームウェア
0x100000  512KB ROM
0x180000  32KB  SRAM セーブ
0x188000  320KB セーブステート × 10スロット（各32KB）
0x1C8000  4KB   メタデータ
```

ターゲットの Flash サイズに合わせて `save_flash.h` の定数を変更すること。

---

## 6. Peanut-GB の統合

### 6.1 コールバック構造

Peanut-GB はヘッダオンリーで、以下のコールバックをユーザーが実装する：

- `gb_rom_read()` — ROM バイトを返す（XIP ポインタから直接 return）
- `gb_cart_ram_read/write()` — SRAM 読み書き
- `gb_lcd_draw_line()` — 1ライン描画（フレームバッファに書き込む）
- `gb_error()` — エラーハンドラ

### 6.2 APU との統合

minigb_apu は Peanut-GB に同梱されている。  
`audio_callback` を登録し、毎フレームの `gb_run_frame()` 後に APU サンプルをリングバッファへ書き込む。

フレームあたりのサンプル数：

```
samples_per_frame = sample_rate / fps
= 32768 / 59.727 ≈ 549
```

端数処理に注意。int の切り捨てが積み重なるとテンポがずれる（PicoCalc では 0.117% 遅れていた）。

---

## 7. 表示関連の非自明な点

### 7.1 LovyanGFX の print() は 0x20 未満を無視する

`LGFXBase.cpp` の `print()` は ASCII 制御文字（0x00〜0x1F）をフィルタリングする。  
CP437 の矢印文字（↑↓←→ は 0x18〜0x1B）はこの範囲に入るため、`print()` では表示できない。  
代替として `^v<>` などの ASCII 文字で代用するか、`drawChar()` で直接描画すること。

### 7.2 差分描画の効果

LovyanGFX の差分描画（変化ピクセルのみ転送）は SPI 帯域を大幅に削減できる。  
`gb_fb[144][160]` を前フレームと比較し、変化した行のみ転送するだけで効果がある。

---

## 8. キーボード・入力に関する注意（PicoCalc 固有）

新ターゲットへの移植時には不要だが、参考として残す。

### 8.1 kbd_init() の順序

`kbd_init()` は `lcd_init()` より**前**に呼ぶ必要がある。  
`lcd_init()` の遅延中に STM32 が I2C を初期化するため、SCL/SDA が浮動だと STM32 の I2C 起動に失敗する。

### 8.2 STM32 ファームウェアの未実装機能

PicoCalc の STM32 はキーボードコントローラであり、以下のレジスタが未実装：

- `I2C 0x0A` — LCD バックライト輝度（実装したが無反応。ファームウェア未対応）
- `I2C 0x0B` — バッテリー残量（NACK が返る。clockworkpi/PicoCalc#20 で既知の問題）

---

## 9. USB stdio の無効化

```cmake
pico_enable_stdio_usb(target 0)
```

USB stdio（TinyUSB）を有効にすると SPI0 および DMA と競合し、SD カードのマウントに失敗する。  
デバッグ出力が必要な場合は UART stdio を使うこと。

---

## 10. ビルド環境の注意点

- `PICO_BOARD=pico2` かつ `-DPICO_PLATFORM=rp2350` の両方が必要。片方だけでは正しくビルドされない。
- `compile_commands.json` を生成する場合は `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` を追加。
- clangd は RP2350 のクロスコンパイル環境を正しく認識しないため false positive が出る。実際のビルドは CMake/Ninja で確認すること。

---

## 11. 移植の推奨順序

```
Phase 1: 構造整理（機能追加なし）
  → emu/ui/hal/boards のディレクトリ構成を作る
  → PicoCalc ビルドが通り続けることを確認

Phase 2a: タッチ確認実装
  → FT3168 から座標が取れるか確認
  → 2点タッチの実装可能性を調査（1点は取れるが2点は要確認）
  → GB プレイに「移動＋ボタン同時押し」は必要なので最終的に2点は欲しい

Phase 2b: 音声確認実装
  → ES8311 I2C 初期化 → PIO I2S から正弦波を出す
  → DMA IRQ 駆動の骨格（g_afifo 流用）を確認
  → サンプルレートを整数に揃えられるか検証

Phase 3: ディスプレイ実装
  → カスタム QSPI ドライバ（LovyanGFX 非対応のため独自実装）
  → Display HAL に差し込む

Phase 4: 統合・GB エミュレーション起動
  → emu 層はほぼそのまま使えるはず
```
