# RP2350-GB-Kaeru - Development Environment Setup

## 1. 概要

RP2350 系デバイス向け Game Boy エミュレータの開発環境セットアップ手順。

対応ターゲット：
- **PicoCalc** (`picocalc_gb_kaeru`) — ClockworkPi PicoCalc
- **AMOLED** (`amoled_gb_kaeru`) — Waveshare RP2350-Touch-AMOLED-1.8

開発方針：

- CLI ベース開発
- Claude Code など AI 支援開発を前提
- Pico SDK + CMake + Ninja を採用
- VSCode は補助用途
- compile_commands.json を生成し、AI 補完を強化

対象OS：

- Ubuntu 24.04 LTS
- WSL2 Ubuntu 24.04

---

## 2. 必要パッケージ

```
sudo apt update

sudo apt install -y \
  git \
  cmake \
  ninja-build \
  gcc-arm-none-eabi \
  libnewlib-arm-none-eabi \
  build-essential \
  pkg-config \
  python3 \
  unzip \
  wget
```

---

## 3. Pico SDK 導入

```
mkdir -p ~/dev
cd ~/dev

git clone https://github.com/raspberrypi/pico-sdk.git
cd pico-sdk
git submodule update --init
```

環境変数を設定する（direnv を使う場合は 3.1 を参照）：

```
echo 'export PICO_SDK_PATH=$HOME/dev/pico-sdk' >> ~/.bashrc
source ~/.bashrc
```

確認：

```
echo $PICO_SDK_PATH
```

---

### 3.1 direnv による自動環境変数設定（推奨）

```
sudo apt install -y direnv
echo 'eval "$(direnv hook bash)"' >> ~/.bashrc
source ~/.bashrc
```

プロジェクトルートに `.envrc` が用意されているので、初回のみ許可する：

```
cd ~/Projects/PicoCalc-GB-Kaeru
direnv allow .
```

以降、ディレクトリに入るたびに `PICO_SDK_PATH` が自動設定される。

---

## 4. picotool 導入

UF2 書き込み補助に使用する。

```
sudo apt install libusb-1.0-0-dev

cd ~/dev
git clone https://github.com/raspberrypi/picotool.git
cd picotool
mkdir build && cd build
cmake ..
make -j$(nproc)
sudo make install
```

確認：

```
picotool version
```

---

## 5. リポジトリのクローン

```
cd ~/Projects
git clone --recurse-submodules <repo-url> PicoCalc-GB-Kaeru
cd PicoCalc-GB-Kaeru
```

サブモジュールが未取得の場合：

```
git submodule update --init --recursive
```

---

## 6. ビルド

```
cd ~/Projects/RP2350-GB-Kaeru

cmake -S . -B build \
  -G Ninja \
  -DPICO_BOARD=pico2 \
  -DPICO_PLATFORM=rp2350 \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# 全ターゲットをビルド
cmake --build build -j$(nproc)

# または個別に
cmake --build build --target picocalc_gb_kaeru -j$(nproc)
cmake --build build --target amoled_gb_kaeru   -j$(nproc)
```

生成物：

```
build/picocalc_gb_kaeru.uf2   # PicoCalc 向け
build/amoled_gb_kaeru.uf2     # AMOLED 向け
build/amoled_sound_test.uf2   # AMOLED 音声単体テスト
build/amoled_touch_test.uf2   # AMOLED タッチ単体テスト
```

---

## 7. デバイスへの書き込み

BOOTSEL ボタンを押しながら USB 接続し、USB マスストレージとして認識されたら：

```
# PicoCalc
cp build/picocalc_gb_kaeru.uf2 /media/$USER/RPI-RP2/

# AMOLED
cp build/amoled_gb_kaeru.uf2 /media/$USER/RPI-RP2/
```

または picotool を使う場合：

```
picotool load build/amoled_gb_kaeru.uf2 --force
picotool reboot
```

### デバッグ出力

| ターゲット | USB stdio | UART |
|-----------|-----------|------|
| `picocalc_gb_kaeru` | 無効（TinyUSB が SPI/DMA と競合するため） | 無効 |
| `amoled_gb_kaeru` | **有効**（USB CDC） | 無効 |
| `amoled_sound_test` | **有効** | 無効 |
| `amoled_touch_test` | **有効** | 無効 |

AMOLED ターゲットは USB 接続中にターミナルを開くと printf 出力を確認できる：

```
# Linux / WSL
screen /dev/ttyACM0 115200

# または minicom, picocom 等
```

---

## 8. VSCode / clangd 設定

推奨拡張：

- C/C++ (Microsoft)
- clangd
- CMake Tools

clangd 設定（`.vscode/settings.json` は既に用意済み）：

```json
{
  "clangd.arguments": [
    "--compile-commands-dir=build"
  ]
}
```

compile_commands.json をルートへリンク（初回のみ）：

```
ln -sf build/compile_commands.json .
```

> clangd が RP2350 のクロスコンパイル環境を完全には認識できないため、IDE 上に
> 誤検知のエラーが表示されることがある。実際のビルドエラーは cmake --build で確認する。

---

## 9. 開発フロー

```
コード変更
  ↓
cmake --build build --target <ターゲット> -j$(nproc)
  ↓
UF2 コピー → 実機動作確認
  ↓
PROGRESS.md を更新（チェックボックス・決定事項ログ）
  ↓
git commit（動作確認のたびにコミット）
  ↓
git push
```

> **注意:** `amoled_gb_kaeru` の変更が `picocalc_gb_kaeru` に影響しないことを定期的に確認する。
> 共有コード（`src/emu/gb/gb_core.c` 等）を変更した場合は両ターゲットをビルドしてエラーがないことを確かめること。

### PROGRESS.md の更新ルール

- タスクが完了したら **チェックボックスを埋める**（`[ ]` → `[x]`）
- 実機確認が取れたら **日付付きで記録する**（例: `実機確認済み 2026-05-22`）
- 方針・仕様を決めたら **決定事項ログに追記する**
- コミット前に更新し、コードの変更と一緒にコミットする

### コミット粒度の指針

- **単一 Issue 単位でコミット** — 1コミット＝1つの目的
- **実機確認が取れたらすぐコミット**
- **大きくなる前にコミット**

```
# 例
feat(audio): Core 1 移行・DMA IRQ 駆動
fix(input): ESC キーでメニューが閉じてしまう問題を修正
docs: 開発環境ドキュメントを現状に合わせて更新
```

---

## 10. トラブルシューティング

### PICO_SDK_PATH エラー

```
echo $PICO_SDK_PATH
source ~/.bashrc
```

### gcc-arm-none-eabi が見つからない

```
arm-none-eabi-gcc --version
sudo apt install gcc-arm-none-eabi
```

### UF2 が認識されない

- BOOTSEL 押下を確認
- USB ケーブルがデータ転送対応であることを確認
- `dmesg | tail` で USB 認識状況を確認

### compile_commands.json が生成されない

cmake 実行時に `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` が付いているか確認する。
