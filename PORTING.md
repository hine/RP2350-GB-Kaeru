# 新ボード対応ガイド

RP2350-GB-Kaeru に新しいボードを追加する際の知見をまとめたドキュメント。  
PicoCalc（SPI LCD + PWM 音声 + I2C キーボード）および AMOLED（QSPI + I2S + タッチ）の実装から得た経験をベースにしている。

---

## 1. 流用できる資産

以下のモジュールはボード非依存で、新ターゲットにほぼそのまま持ち込める。

| モジュール | ファイル | 備考 |
|---|---|---|
| GB エミュレーションコア | `src/emu/gb/gb_core.c/h` | Peanut-GB ラッパー。ROM XIP・APU・セーブステート統合済み |
| SRAM セーブ | `src/storage/save_flash.c` | マジック + ROM タイトル検証ロジックを維持すること（→ 4節） |
| セーブステート | `src/storage/save_flash.c` | Flash レイアウト定数はボードごとに調整が必要 |
| Flash メタデータ | `src/storage/flash_meta.c/h` | ROM 有効フラグ・SRAM 有効フラグ・セーブステート有効フラグ |
| ROM Flash 書き込み | `src/storage/rom_flash.c/h` | XIP オフセットを Flash レイアウトに合わせること |
| SPSC リングバッファ | `src/emu/gb/gb_core.c` 内 `g_afifo` | APU → 音声出力の橋渡し。サンプルレートが変わっても構造は流用できる |
| メニューロジック | `src/ui/menu/menu.c/h` | 表示部分は Display HAL に差し替えが必要 |

---

## 2. 音声アーキテクチャ（最重要）

### 2.1 コア分担の基本構成

```
Core 0:
  GB エミュレーション
  APU サンプル生成 → g_afifo（SPSC リングバッファ）書き込み
  音声 DMA IRQ（リングバッファ → DMA バッファ → 出力）← 必ず Core 0

Core 1:
  ディスプレイ描画
  入力ポーリング
  multicore_lockout_victim_init()
```

### 2.2 音声 DMA IRQ は必ず Core 0 に置く

**Core 1 に音声 DMA IRQ を置いてはいけない。**

Flash 書き込み時に `multicore_lockout_start_blocking()` を呼ぶと Core 1 が停止する。  
この停止中にハードウェアチェーン DMA が走ると `TRANS_COUNT=0` のまま暴走し、スピーカーから大きなノイズが出る。

Core 0 に IRQ を置いた場合、lockout の影響を受けない。Flash 書き込み中は単に無音になるだけ。

### 2.3 IRQ ハンドラは SRAM に配置する

```c
void __not_in_flash_func(audio_dma_irq_handler)(void) { ... }
```

Flash 書き込み中は XIP が無効になる。IRQ ハンドラが Flash 上にあるとハードフォルトする。  
`__not_in_flash_func` は必須。

### 2.4 出力方式の選択

| 項目 | PWM（PicoCalc の例） | I2S（AMOLED の例） |
|---|---|---|
| 出力先 | GP26/27 直結 PWM | PIO → I2S コーデック（ES8311） |
| サンプルレート | ~32768Hz（TIMER_WRAP で近似） | 整数で割り切れる値（32000Hz 等）が使える |
| テンポずれ | int 切り捨ての累積誤差に注意（→ 下記） | 整数サンプルレートなら根本解決 |
| DMA 構成 | シングルチャンネル + ソフト再チェーン | 同じパターンが使える |

SPSC リングバッファ（`g_afifo`）と DMA IRQ 駆動の骨格はどちらの方式でも共通。  
出力部分（PWM / I2S PIO）だけを差し替えるイメージで実装できる。

**テンポずれ対策：**  
`samples_per_frame = sample_rate / fps` の端数を切り捨てると毎フレーム誤差が累積する。  
Bresenham 法などで端数を補正すること（AMOLED では 785/1024 補正を適用）。

### 2.5 ハードウェアチェーン DMA は使わない

DMA の `chain_to` 機能は Flash 書き込みとの組み合わせで前述の暴走問題が起きる。  
ソフトウェア再チェーン（IRQ ハンドラで次の転送を手動セットアップ）を使うこと。

---

## 3. マルチコア・Flash 書き込み

### 3.1 lockout の必須性

Core 1 は Flash 上のコード（表示ドライバ等）を実行している。  
Flash 消去・書き込み中に Core 1 が Flash へアクセスすると即座にハードフォルトする。

```c
multicore_lockout_start_blocking();
// ... flash_range_erase / flash_range_program ...
multicore_lockout_end_blocking();
```

Core 1 側では起動時に一度だけ呼ぶ：

```c
multicore_lockout_victim_init();
```

### 3.2 メニュー表示中の描画競合

メニュー表示中に Core 1 がフレームを上書きする競合を防ぐため、`volatile bool g_menu_active` フラグが必要。  
Core 1 の描画ループで「`g_menu_active` が true なら描画をスキップ」というガードを入れること。

---

## 4. セーブデータの検証

### 4.1 ブランクチェックでは不十分

Flash の SRAM セーブ領域が `0xFF` かどうかだけで判定すると、  
別ビルドや別 ROM のゴミデータを誤ロードしてゲームセーブが壊れる。

### 4.2 マジック + ROM タイトル照合

Flash メタデータにマジックバイト + ROM タイトル（11 バイト）を記録し、ロード前に照合する。

```c
#define SRAM_MAGIC 0xCAFEBABE
// メタデータ領域に { magic, rom_title[11] } を保存
// ロード時に両方が一致した場合のみ復元する
```

### 4.3 `is_dirty()` ではなく `consume_dirty()` を使う

Peanut-GB の `gb_is_dirty()` はラッチ型で、一度 true になると次に呼ぶまで true を返し続ける。  
毎フレームのデバウンスループで使うと「カウントが 0 に到達しない」バグになる。

```c
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

### 5.2 起動戦略

```
起動時:
  1. Flash メタデータを確認（ROM 有効フラグ）
  2. SD がある場合: ROM タイトル照合 → 差分があれば再フラッシュ
  3. ROM 有効フラグが立っていれば SD なしで起動可能
```

### 5.3 Flash レイアウト

現在の共通レイアウト（両ターゲット共通）：

```
0x000000  1 MB   ファームウェア
0x100000  512 KB ROM
0x180000   32 KB SRAM セーブ
0x188000  320 KB セーブステート × 10スロット（各 32 KB）
0x1D8000   32 KB 予約済み
0x1E0000    4 KB メタデータ
```

新ボードで Flash サイズが異なる場合は `save_flash.h` の定数を変更する。

---

## 6. Peanut-GB の統合

### 6.1 コールバック構造

Peanut-GB はヘッダオンリーで、以下のコールバックをユーザーが実装する：

- `gb_rom_read()` — ROM バイトを返す（XIP ポインタから直接 return）
- `gb_cart_ram_read/write()` — SRAM 読み書き
- `gb_lcd_draw_line()` — 1ライン描画（フレームバッファに書き込む）
- `gb_error()` — エラーハンドラ

### 6.2 APU との統合

`audio_callback` を登録し、毎フレームの `gb_run_frame()` 後に APU サンプルをリングバッファへ書き込む。

```
samples_per_frame = sample_rate / fps ≈ sample_rate / 59.727
```

端数を Bresenham 法等で補正しないとテンポがずれる。

---

## 7. 表示ドライバ

### 7.1 SPI LCD（LovyanGFX が使える場合）

LovyanGFX が対応しているパネルであれば `lgfx_config.hpp` を新ボード向けに作るだけで済む。  
注意点：
- `invert` / `rgb_order` / `setRotation()` はパネルごとに調査が必要
- `print()` は ASCII 制御文字（0x00〜0x1F）を無視する。CP437 の矢印文字（0x18〜0x1B）は `drawChar()` で描くこと
- 差分描画（変化行のみ転送）は SPI 帯域を大幅に削減できる

### 7.2 QSPI / LovyanGFX 非対応パネル

AMOLED のように LovyanGFX が対応していないパネルは独自ドライバが必要。  
`src/drivers/display/qspi_pio.c` と `amoled_1in8.c` が参考実装。  
`src/hal/display.h` の Display HAL を実装することでメニュー等の共有コードはそのまま使える。

### 7.3 オーバークロックと表示タイミング

クロックを変更した場合（AMOLED では 200MHz）、PIO の分周比を動的に計算し直すこと。  
固定値のままだと表示が乱れる場合がある。

---

## 8. 入力

### 8.1 I2C キーボード（PicoCalc）

`kbd_init()` はディスプレイ初期化より**前**に呼ぶ必要がある。  
LCD 初期化の遅延中に STM32 が I2C を初期化するため、SCL/SDA が浮動だと STM32 が起動しない。

STM32 ファームウェアの未実装機能（現時点）：
- `I2C 0x0A` — LCD バックライト輝度（無反応）
- `I2C 0x0B` — バッテリー残量（NACK）

### 8.2 静電容量タッチ（AMOLED / FT3168）

FT3168 は **1点タッチのみ**対応（2点目は応答なし）。  
「方向入力 + ボタン同時押し」が必要な場面は物理ボタン（GPIO）で補完する。  
I2C バスを音声コーデックと共有する場合、両ドライバにタイムアウトを設けること（フリーズ防止）。

### 8.3 物理ボタン（GPIO）

AMOLED の POWER ボタン（GPIO18）は USB 給電中は AXP2101 が電源 OFF を無効化するため、  
短押しを A ボタンとして使用できる。バッテリー駆動時は長押しで電源 OFF になるため要注意。

---

## 9. USB stdio の注意

```cmake
pico_enable_stdio_usb(target 0)
```

USB stdio（TinyUSB）を有効にすると SPI および DMA と競合し、SD カードのマウントに失敗することがある。  
デバッグ出力が必要な場合は UART stdio を使うこと。  
リリースビルドでは無効化する。

---

## 10. ビルド設定

- `PICO_BOARD=pico2` かつ `-DPICO_PLATFORM=rp2350` の両方が必要。片方だけでは正しくビルドされない。
- `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` で `compile_commands.json` を生成する（clangd 補完用）。
- clangd は RP2350 クロスコンパイル環境を完全には認識しないため false positive が出る。実際のビルドは CMake/Ninja で確認すること。
- 共有コード（`src/emu/gb/gb_core.c` 等）を変更した場合は全ターゲットをビルドしてエラーがないことを確認する。

---

## 11. 新ボードの追加手順

```
Phase 1: ディレクトリ・CMake 構成（機能追加なし）
  → src/boards/<board>/ を作成
  → CMakeLists.txt に新ターゲットを追加
  → 既存ターゲットのビルドが通り続けることを確認

Phase 2a: 入力確認
  → タッチ / キーボード / GPIO から座標・キーコードが取れるか確認
  → 同時入力の可否を確認（タッチは特に注意）

Phase 2b: 音声確認
  → コーデック初期化 → 正弦波出力で動作確認
  → DMA IRQ 駆動の骨格（g_afifo 流用）を確認
  → サンプルレートと Bresenham 補正値を決定

Phase 3: 表示ドライバ
  → LovyanGFX 対応パネルなら lgfx_config.hpp を作成
  → 非対応パネルは独自ドライバを実装して Display HAL に差し込む
  → GB 画面（160×144）の 2× スケール表示を確認

Phase 4: 統合
  → main_<board>.c を作成し emu 層・音声・表示・入力を結合
  → Flash レイアウト定数を確認・調整
  → セーブ・ロード・メニューを動作確認
```
