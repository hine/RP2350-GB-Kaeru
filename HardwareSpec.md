# ハードウェア仕様

本ドキュメントは対応ターゲット両機のハードウェア仕様をまとめたものです。

---

# Part 1: PicoCalc ハードウェア仕様

調査日: 2026-05-22

## 1. ピン割り当て一覧

| 機能 | 信号 | GPIO | インターフェース |
|---|---|---|---|
| LCD | SCK | GP10 | SPI1 |
| LCD | MOSI | GP11 | SPI1 |
| LCD | MISO | GP12 | SPI1 |
| LCD | CS | GP13 | SPI1 |
| LCD | DC | GP14 | GPIO |
| LCD | RST | GP15 | GPIO |
| キーボード | SDA | GP6 | I2C1 |
| キーボード | SCL | GP7 | I2C1 |
| SD | MISO | GP16 | SPI0 |
| SD | CS | GP17 | SPI0 |
| SD | SCK | GP18 | SPI0 |
| SD | MOSI | GP19 | SPI0 |
| SD | Detect | GP22 | GPIO |
| 音声 L | PWM | GP26 | PWM |
| 音声 R | PWM | GP27 | PWM |
| PSRAM | CS | GP20 | — |
| PSRAM | SCK | GP21 | — |
| PSRAM | MOSI | GP2 | — |
| PSRAM | MISO | GP3 | — |

---

## 2. LCD

### 2.1 概要

| 項目 | 内容 |
|---|---|
| パネル解像度 | 320 × 480 |
| 実効表示解像度 | **320 × 320**（下部 160 行はブランク） |
| LCDコントローラ | 初期ロット: **ILI9488** / 最新ロット: **ST7365P** |
| ST7365P互換性 | ILI9488 と 99% コマンド互換（3-bit カラーモード非対応） |
| インターフェース | **4線式 SPI1** |
| SPI クロック | 仕様上 ~15MHz だが 25〜75MHz での動作報告あり |
| ピクセルフォーマット | 18-bit（RGB666） |
| **色並び順** | **BGR**（MADCTL 0x36 = 0x48、bit3 = BGR フラグ = 1）実機確認済み 2026-05-25 |
| バックライト制御 | STM32 キーコントローラ経由 I2C（I2C レジスタ 0x0A bit7=1） |

### 2.2 初期化シーケンス（ILI9488 / ST7365P 共通）

公式 helloworld サンプル（`clockworkpi/PicoCalc` リポジトリ内 `lcdspi.c`）に基づく。

1. GPIO 設定 + SPI1 を 25MHz で初期化
2. RST でハードウェアリセット
3. コマンドシーケンス送信：
   - Positive/Negative Gamma Control (0xE0, 0xE1)
   - Power Control 1/2 (0xC0, 0xC1)
   - VCOM Control (0xC5)
   - Memory Access Control (0x36, 値 **0x48** = MX(bit6) + **BGR(bit3)**)
     → **bit3=1 は BGR 色並び順を意味する**。ソフトウェアから RGB 順でデータを送ると R と B が反転して表示される。
   - Pixel Interface Format (0x3A, 値 0x66 = 18-bit SPI)
   - Frame Rate Control (0xB1, 0xA0)
   - Display Inversion On (0x21)
   - Function Control (0xB6)
   - Sleep Out (0x11) → 120ms wait
   - Display On (0x29) → 120ms wait

### 2.3 GB 表示への適用

GB の画面解像度は 160 × 144。PicoCalc LCD（320 × 320）への表示方法：

| 方法 | スケール | 表示サイズ |
|---|---|---|
| 等倍 | 1x | 160 × 144（中央寄せ余白あり） |
| 整数2倍 | 2x | 320 × 288（横いっぱい、縦余白あり） |
| アスペクト比維持最大 | 2x | 320 × 288 が現実的な最大 |

DMA 転送の活用を検討する（RP2350 の DMA コントローラ使用）。

### 2.4 LovyanGFX 設定（実機確認済み）

`src/video/lgfx_config.hpp` における ILI9488 パネル設定の確定値：

| 設定項目 | 値 | 理由 |
|---|---|---|
| `rgb_order` | **`false`**（BGR）| パネルが BGR 色並び順のため。`true`（RGB）にすると全色 R↔B 反転して表示される |
| `invert` | `true` | ILI9488 パネルの自然反転を補正 |
| `setRotation(6)` | 6 | MX=0/MY=0 正常ポートレート |

> **注意**: `rgb_order = true` を設定すると、LovyanGFX が RGB 順でデータを送信し、BGR パネルでは赤と青が入れ替わって表示される。具体的には DMG パレットのグリーンがティール（青緑）に見え、黄色のアイコンが青く表示される。必ず `false` にすること。

### 2.5 参照ソース

- 公式ドライバ: `clockworkpi/PicoCalc` → `Code/picocalc_helloworld/lcdspi/`
- データシート: 同リポジトリ内 `ST7365P_SPEC_V1.0.pdf`

---

## 3. キーボード

### 3.1 概要

| 項目 | 内容 |
|---|---|
| キー数 | 67キー（QWERTY配列 + ファンクションキー） |
| マトリクス | 7行 × 8列 |
| コントローラ | **STM32F103R8T6**（ARM Cortex-M3） |
| 接続方式 | **I2C1** |
| I2C アドレス | **0x1F** |
| I2C 速度 | **10kHz 固定**（これより速いと通信不安定） |
| 割り込み | なし（ポーリング専用） |

### 3.2 I2C レジスタマップ

| レジスタ | アドレス | 説明 |
|---|---|---|
| REG_ID_FIF | 0x09 | キーボード FIFO 読み出し |
| バックライト | 0x0A (bit7=1) | バックライト輝度 (0〜255) |
| バッテリー | 0x0B | バッテリー残量（**ファームウェア依存**。0 を返す場合は未実装 → 診断中 2026-05-25） |

### 3.3 キー読み出し手順

```
1. 0x09 を I2C Write
2. 16ms 待機
3. 2バイト I2C Read

16-bit データフォーマット:
  [15:8] = ASCII キーコード
  [0]    = キー押下フラグ (1=押下, 0=解放)

特殊値:
  0x7E02 = Ctrl キー押下
  0x7E03 = Ctrl キー解放
```

### 3.4 GB 向けキーマッピング（初期案）

| GB 入力 | PicoCalc キー | キーコード |
|---|---|---|
| Up | カーソル上 | 0xB5 |
| Down | カーソル下 | 0xB6 |
| Left | カーソル左 | 0xB4 |
| Right | カーソル右 | 0xB7 |
| A | A キー | 0x61 |
| B | B キー | 0x62 |
| Start | Enter | 0x0A |
| Select | Space | 0x20 |
| Menu | Esc | 0xB1 |

### 3.5 初期化タイミングの注意点（実機確認済み）

**standalone 起動（USB なし）では `kbd_init()` を `lcd_init()` より前に呼ぶこと。**

#### 背景

PicoCalc の standalone 起動では、Pico と STM32 キーボードコントローラが同時に電源 ON になる。
STM32 は起動直後に自分の I2C ペリフェラルを初期化する。
この時点で SCL/SDA に pull-up がなく浮動状態だと、
STM32 が「I2C バスが存在しない」と判断して I2C インターフェースを正常に起動できない。

`lcd_init()` は初期化シーケンスに **120ms × 2 のウエイト**を含み 240ms 以上かかる。
`kbd_init()` を `lcd_init()` の後に呼ぶと、この 240ms の間 GP6/GP7 が浮動状態になる。
STM32 の I2C 初期化はまさにこのウィンドウで起きるため、正常に起動できない。

USB 起動ではキーボードコントローラは PicoCalc 電源ボタンが押されるまで無電源のため、
この問題は発生しない（kbd_init() 完了後に STM32 が起動する）。

#### 正しい初期化順序

```c
int main() {
    stdio_init_all();
    kbd_init();   // ← 必ず lcd_init() より前！SCL/SDA=HIGH を早期確立
    lcd_init();
    // ...
}
```

#### キーボード応答待ち

standalone 起動では kbd_init() 後も STM32 の I2C 起動完了まで数秒かかる。
I2C read/write を Core 1 でリトライするか、応答を確認してからポーリングを開始すること。

```c
// Core 1 での待機例
void kbd_wait_ready(void) {
    uint8_t msg = 0x09;
    for (;;) {
        i2c_init(I2C_KBD_MOD, I2C_KBD_SPEED);  // タイムアウト後の状態をリセット
        gpio_set_function(I2C_KBD_SCL, GPIO_FUNC_I2C);
        gpio_set_function(I2C_KBD_SDA, GPIO_FUNC_I2C);
        gpio_pull_up(I2C_KBD_SCL);
        gpio_pull_up(I2C_KBD_SDA);
        if (i2c_write_timeout_us(I2C_KBD_MOD, I2C_KBD_ADDR, &msg, 1, false, 100000) >= 0)
            break;
        sleep_ms(500);
    }
}
```

### 3.6 参照ソース

- 公式ドライバ: `clockworkpi/PicoCalc` → `Code/picocalc_helloworld/i2ckbd/`

---

## 4. SDカード

### 4.1 概要

| 項目 | 内容 |
|---|---|
| インターフェース | **SPI0** |
| ファイルシステム | FAT32 |
| ライブラリ | **FatFS**（推奨: `no-OS-FatFS-SD-SPI-RPi-Pico` by carlk3） |
| カード挿入検出 | GP22（GPIO） |

### 4.2 パーティション構成（参考）

| パーティション | 種別 | 用途 |
|---|---|---|
| 1 | FAT32 | アプリ・ROM・データ |
| 2 | Linux 32MB | FUZIX用（本プロジェクトでは不使用） |

### 4.3 本プロジェクトでの使用予定

```
/roms/kaeru.gb       ← ROMファイル
/saves/kaeru.sav     ← セーブRAM
/saves/kaeru.slot0.state  ← セーブステート
```

---

## 5. 音声

| 項目 | 内容 |
|---|---|
| 出力方式 | **PWM**（初期実装） / I2S（高音質版） |
| 左チャンネル | GP26 |
| 右チャンネル | GP27 |

PicoCalc-GameBoy (jblanked) は I2S 44.1kHz 16-bit ステレオを使用。
初期実装は PWM で対応し、後で I2S に移行する。

---

## 6. 既存 GB エミュレータ実装（参考）

本プロジェクトの先行実装として以下が存在し、いずれも **Peanut-GB** エンジンを採用。

| プロジェクト | URL | エンジン | 速度 | 特記 |
|---|---|---|---|---|
| PicoCalc-GameBoy | https://github.com/jblanked/PicoCalc-GameBoy | Peanut-GB | 55〜70fps | RP2350対応、I2S音声あり |
| Picocalc_GBEmu | https://github.com/quanliew28/Picocalc_GBEmu | Peanut-GB | 14fps（改善予定） | ROM埋め込み方式 |
| PocketPico | https://github.com/TheKiwil/PocketPico | Peanut-GB | — | フォーク版、セーブ未実装 |
| Picoware | https://github.com/jblanked/Picoware | Peanut-GB他 | — | 総合ファームウェア |

→ **Peanut-GB** が PicoCalc + RP2350 向け GB エミュレーションの事実上の標準。

---

## 7. 情報ソース

- [clockworkpi/PicoCalc (公式)](https://github.com/clockworkpi/PicoCalc)
- [DeepWiki: Main Board](https://deepwiki.com/clockworkpi/PicoCalc/2.1-main-board)
- [DeepWiki: Display and Audio](https://deepwiki.com/clockworkpi/PicoCalc/2.3-display-and-audio)
- [DeepWiki: Programming Guide](https://deepwiki.com/clockworkpi/PicoCalc/5-programming-guide)
- [DeepWiki: SD Card](https://deepwiki.com/clockworkpi/PicoCalc/4.6-sd-card-and-multi-boot-system)
- [フォーラム: GPIO ピン割り当て](https://forum.clockworkpi.com/t/gpio-for-pico-calc-how-to-make-firmware-for-pico-calc/20905)
- [フォーラム: ILI9488 ピン設定](https://forum.clockworkpi.com/t/ili9488-pin-configuration/17547)
- [フォーラム: ST7365P LCD](https://forum.clockworkpi.com/t/new-lcd-screen-st7365p-in-recent-picocalc-commit/17649)

---

---

# Part 2: Waveshare RP2350-Touch-AMOLED-1.8 ハードウェア仕様

調査日: 2026-06-03（実機確認済み）

## 1. ピン割り当て一覧

| 機能 | 信号 | GPIO | インターフェース |
|------|------|------|----------------|
| AMOLED | CS | GP9 | QSPI PIO |
| AMOLED | SCLK | GP10 | QSPI PIO |
| AMOLED | DIO0 | GP11 | QSPI PIO |
| AMOLED | DIO1 | GP12 | QSPI PIO |
| AMOLED | DIO2 | GP13 | QSPI PIO |
| AMOLED | DIO3 | GP14 | QSPI PIO |
| AMOLED | RST | GP15 | GPIO |
| AMOLED | PWR_EN | GP17 | GPIO |
| タッチ / I2C 共有 | SDA | GP6 | I2C1 |
| タッチ / I2C 共有 | SCL | GP7 | I2C1 |
| FT3168 タッチ RST | — | GP5 | GPIO |
| FT3168 タッチ INT | — | GP4 | GPIO |
| ES8311 音声コーデック | SDA/SCL | GP6/7 | I2C1（共有） |
| ES8311 PA 制御 | PA_CTRL | GP19 | GPIO |
| ES8311 I2S DOUT | — | GP20 | PIO（pio2） |
| ES8311 I2S DIN | — | GP21 | PIO（未使用） |
| ES8311 I2S MCLK | — | GP22 | PIO（pio1） |
| ES8311 I2S LRCLK | — | GP23 | ES8311 出力（入力として読む） |
| ES8311 I2S BCLK | — | GP24 | ES8311 出力（入力として読む） |
| SD カード | MISO | GP28 | SPI1 |
| SD カード | MOSI | GP27 | SPI1 |
| SD カード | SCK | GP26 | SPI1 |
| SD カード | CS | GP25 | SPI1 |
| システム | SYS_OUT | GP18 | GPIO（電源キー） |

---

## 2. AMOLED ディスプレイ

| 項目 | 内容 |
|------|------|
| パネル解像度 | **368 × 448 px** |
| コントローラ | QSPI 接続（カスタムドライバ使用、LovyanGFX 非対応） |
| インターフェース | QSPI（4 データライン）/ PIO 駆動（pio0 SM0/1） |
| PIO クロック | div=動的計算（`clock_get_hz(clk_sys) / (75MHz × 2)`）、SCLK ≈ 75MHz |
| 色フォーマット | RGB565 big-endian（`AMOLED_COLOR()` マクロで変換） |
| 輝度制御 | I2C コマンド（`amoled_1in8_set_brightness()`） |

### GB 画面レイアウト

```
y=0〜15    (16px)  ステータスバー（セーブ操作タッチゾーン）
y=16〜303  (288px) GB 画面（2× スケール 320×288、x=24〜343 中央寄せ）
y=304〜447 (144px) 操作 UI（左: 十字キー / 右: ボタン 4象限）
```

---

## 3. タッチコントローラ（FT3168）

| 項目 | 内容 |
|------|------|
| コントローラ | FT3168（Waveshare 独自ファームウェア） |
| I2C アドレス | 0x38 |
| I2C バス | i2c1（GP6/7、400kHz） |
| タッチ点数 | **1点のみ**（実機確認済み、2点目は応答なし） |
| 動作モード | ポイントモード（`FT3168_MODE_POINT`） / ジェスチャーモード（`FT3168_MODE_GESTURE`） |
| ジェスチャー | UP / DOWN / LEFT / RIGHT / CLICK / DOUBLE_CLICK（reg 0xD3） |
| INT ピン | GP4（EDGE_RISE 割り込み） |

### 1点タッチ制限について

2点タッチの実装可否を調査した結果、**FT3168 は1点しか返さない**ことを実機で確認。
GB プレイに必要な「十字キー + ボタン同時押し」は画面の左右分割レイアウトで対応（左: 十字キー、右: ボタン）。

---

## 4. 音声コーデック（ES8311）

| 項目 | 内容 |
|------|------|
| コーデック | ES8311 |
| I2C アドレス | 0x18（chip ID: 0x1183） |
| I2C バス | i2c1（GP6/7、400kHz、FT3168 と共有） |
| 動作モード | **マスターモード**（ES8311 が BCLK/LRCLK を生成） |
| サンプルレート | **32000 Hz** |
| MCLK | 6,144,000 Hz（= 32000 × 192）、PIO1 SM0 で生成 |
| BCLK | 2,048,000 Hz（= MCLK / 3 × 4 / bclk_div 4） |
| LRCLK | 32,000 Hz（= system_clock / 256） |
| 分解能 | 16bit stereo |
| PA 制御 | GP19 HIGH でアンプ有効 |

### I2S PIO 構成

| PIO | SM | 用途 |
|-----|-----|------|
| pio0 | 0,1 | QSPI 表示（既存） |
| **pio1** | 0 | MCLK 生成（6.144MHz 矩形波） |
| **pio2** | 0 | I2S DOUT（ES8311 スレーブ送信） |

ES8311 がマスターで BCLK/LRCLK を生成するため、RP2350 のクロック変更（オーバークロック）の影響を受けない。
MCLK 分周器は `clock_get_hz(clk_sys)` 実行時参照で自動補正される。

---

## 5. SD カード

| 項目 | 内容 |
|------|------|
| インターフェース | SPI1（GP26-28、CS=GP25） |
| ボーレート | 12 MHz |
| ファイルシステム | FAT32 |
| ライブラリ | no-OS-FatFS-SD-SPI-RPi-Pico |
| DMA IRQ | DMA_IRQ_1（音声の DMA_IRQ_0 と分離） |

オーバークロック（200MHz）時も `clk_peri=200MHz` を正しく設定することで、`spi_init()` が分周比を自動計算し 12MHz を維持する。

---

## 6. システムクロック

| 設定 | 値 | 備考 |
|------|-----|------|
| sys_clk | **200 MHz**（オーバークロック） | `set_sys_clock_khz(200000, true)` |
| clk_peri | 200 MHz | SPI/I2C 分周比の基準 |
| QSPI PIO | ≈75 MHz SCLK | `div = sys_clk / (75MHz × 2)` 動的計算 |
| Audio MCLK PIO | ≈6.144 MHz | `div = sys_clk / (6144000 × 5)` 動的計算 |
| フレームタイマー | 59.727 fps（crystal 基準） | クロック変更の影響なし |

---

## 7. 情報ソース

- Waveshare 公式サンプル（`samples/RP2350-Touch-AMOLED-1.8/`）
- `samples/RP2350-Touch-AMOLED-1.8/C/02-ES8311/` — ES8311 / I2S 実装参考
- `src/boards/waveshare_touch_amoled_1_8/board_config.h` — ピン定義
