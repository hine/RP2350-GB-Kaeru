# 開発進捗

---

## Claude との作業ルール

### 問題発生時のドキュメントファースト

1. **実行前に** `SCRATCH.md` へ問題・仮説・対策を記載する
2. 試行のたびに結果を `SCRATCH.md` の試行ログへ追記する（ユーザーが OK と言うまで続ける）
3. 解決したら要点を `PROGRESS.md` の決定事項ログに転記し、`SCRATCH.md` のセクションを削除する

このルールにより、`/clear` でコンテキストがリセットされても `SCRATCH.md` を読めば続きから作業できる。

### ファイル作成前の確認事項

- `git log --diff-filter=A -- <ファイル名>` でそのファイルの初回コミットを確認する
- このリポジトリの md ファイルは全て本プロジェクト内で作成されたもの（フォーク元由来ではない）

---

最終更新: 2026-06-03

---

## 現在のフォーカス

**AMOLED ターゲット** → ✅ 基本実装完了（音声・表示・タッチ・セーブ・200MHz 動作確認済み）

---

---

## マイルストーン進捗

### Milestone 0: 開発環境構築 ✅ 完了

- [x] Pico SDK 導入（`~/dev/pico-sdk`）
- [x] CMake / Ninja ビルド確認
- [x] UF2 生成確認（`build/picocalc_gb_kaeru.uf2`）
- [x] PicoCalc への書き込み確認
- [x] `compile_commands.json` 生成設定

### Milestone 1: PicoCalc 基本I/O ✅ 完了

- [x] PicoCalc LCD 仕様調査（→ `HardwareSpec.md`）
- [x] PicoCalc キーボード 仕様調査（→ `HardwareSpec.md`）
- [x] PicoCalc SDカード 仕様調査（→ `HardwareSpec.md`）
- [x] LCD 初期化・Hello World 表示（実機確認済み 2026-05-22）
- [x] キーボード入力取得（実機確認済み 2026-05-22）
- [x] SDカード読み込み確認（実機確認済み 2026-05-22）
- [x] スピーカー出力（PWM 12bit DMA IRQ として Milestone 7 で完了）

### Milestone 2: GBコア組み込み ✅ 完了

- [x] GBエミュレーションコア選定（Peanut-GB 採用）
- [x] emu 層構成決定（`emu/gb/gb_core.c`）
- [x] ROM 読み込み（初回起動時に Flash へ書き込み、XIP で直読み）
- [x] CPU 実行（実機 ~60fps 確認済み 2026-05-22）
- [x] 映像バッファ取得（`gb_fb[144][160]` に lcd_draw_line コールバックで蓄積）

### Milestone 3: 画面表示 ✅ 完了

- [x] 160×144 → 2x スケール（320×288）で LCD へ表示（実機確認済み 2026-05-24）
- [x] LovyanGFX RGB565 差分描画（変化行のみ転送）
- [x] フレーム更新ループ（~60fps 達成 2026-05-22）

### Milestone 4: 入力対応 ✅ 完了

- [x] 十字キー（KEY_UP/DOWN/LEFT/RIGHT）
- [x] WASD 追加（2026-05-24）
- [x] A / B（`,`/`[`=A、`.`/`]`=B、2026-05-24 更新）
- [x] Start / Select（BS/Enter=Start、Del=Select、2026-05-25 更新）
- [x] press/release 状態管理（「ポチ」押しの取りこぼし修正 実機確認済み 2026-05-22）
- [x] ESC でメニュー開く（2026-05-25）

### Milestone 5: 対象ROM起動 ✅ 完了

- [x] 「カエルの為に鐘は鳴る」タイトル表示（実機確認済み 2026-05-23）
- [x] ゲーム開始（実機確認済み 2026-05-23）
- [x] 操作可能状態（standalone 確認済み 2026-05-23）

### Milestone 6: セーブ対応 ✅ 完了

- [x] SRAM 保存 / 読み込み
- [x] Flash への保存（ゲーム内セーブ後 ~1秒デバウンスで自動保存、2026-05-25）
- [x] SRAM セーブ検証（マジック + ROM タイトル照合、ゴミデータ・別 ROM セーブを排除、2026-05-25）
- [x] ゲーム内セーブ検出修正（`gb_core_consume_dirty()` 追加、2026-05-25）
- [x] SD へのバックアップ・Flash への書き戻し（メニュー UI から操作可能）

### Milestone 7: 音声対応 ✅ 完了

- [x] GB APU 出力（minigb_apu 統合、実機確認済み 2026-05-23）
- [x] PicoCalc スピーカー再生（PWM 12bit + DMA IRQ、実機確認済み 2026-05-24）
- [x] RP2350 クロック不一致修正（TIMER_WRAP 3814→4582、音程ズレ +2.4 半音を解消）
- [x] Flash 書き込み中の「ザザッ」ノイズ解消（ハードウェアチェーン DMA → シングルチャンネル + Core 0 DMA IRQ、実機確認済み 2026-05-25）
- [x] テンポずれ修正（TIMER_WRAP 4582→4578・AUDIO_SAMPLES 548→549、誤差 0.117%→0.004%、実機確認済み 2026-05-25）

### Milestone 8: GBC 対応 ⬜ 予定なし

対象ゲームが DMG タイトルのため実装予定なし。

### Milestone 9: 携帯機化 ✅ 完了

- [x] LovyanGFX 移行（RGB565 差分描画、等倍テキスト UI、実機確認済み 2026-05-24）
- [x] LCD 描画 Core 1 移行（ダブルフレームバッファ + mutex、実機確認済み 2026-05-24）
- [x] ILI9488 BGR 色並び順修正（`rgb_order=false`、全色正常表示、実機確認済み 2026-05-25）
- [x] ステータスバー UI（上部: F キーバインド・バッテリー・ストレージアイコン / 下部: ゲームキーヒント）
- [x] バッテリー残量表示 UI（緑/黄/赤。STM32 未実装のため "--" 表示で運用）
- [x] Flash メタデータ（`flash_meta.h/c`、ROM 有効フラグ + SRAM 有効フラグ、2026-05-25）
- [x] SD オプション起動（ROM Flash 記録済みなら SD なしで起動可能、2026-05-25）
- [x] セーブステート（F2=保存 / F3=ロード / F4=スロット切替、Flash 10 スロット、実機確認済み 2026-05-25）
- [x] ソフトリセット F1（GB 再初期化 + Flash SRAM リロード、実機確認済み 2026-05-25）
- [x] F1 スリープ（dormant）撤去（F2 ステートセーブ後に電源断で代替、powman 依存コード全削除 2026-05-25）
- [x] ファンクションキー確定（F1=ソフトリセット / F2=ステートセーブ / F3=ステートロード / F4=スロット切替）
- [x] メニュー UI（ESC 開く・B 閉じる、実機確認済み 2026-05-25）
  - パレット切替（DMGGreen / Mono / Sepia / GBPocket）リアルタイムプレビュー
  - 音声 ON/OFF トグル
  - SD バックアップ（完了トースト表示）
  - SD から Flash 復元（確認ダイアログ + 完了トースト）
  - Flash 全消去（確認ダイアログ）
  - 設定（パレット・音声）を Flash に保存・起動時ロード
- [x] LCD バックライト制御断念（STM32 ファームウェアが I2C 0x0A 未実装。メニューから削除済み）

---

## 保留・未解決事項

| 項目 | 内容 |
|---|---|
| バッテリー I2C 0x0B | STM32 ファームウェアが未実装（NACK）。clockworkpi/PicoCalc#20 で同様の既知問題。UI は "--" 表示で対応済み |
| LCD バックライト（I2C 0x0A） | 公式プロトコルを実装したが実機で無反応。STM32 ファームウェア未対応の可能性。メニューから削除済み（Flash 設定保存形式は維持） |

---

## 決定事項ログ

| 日付 | 決定内容 | 理由 |
|---|---|---|
| 2026-05-22 | 実行ファイル名を `picocalc_gb_kaeru` とする | ターゲットROM名と一致させ識別しやすくするため |
| 2026-05-22 | Peanut-GB を採用 | 軽量・移植性高い・RP2350 実績あり |
| 2026-05-22 | LCD は SPI1 (GP10-15)、I2C キーボード (GP6/7, 0x1F)、SD は SPI0 (GP16-19) | PicoCalc ハードウェア仕様調査結果より |
| 2026-05-22 | compat/hardware/rtc.h に no-op スタブを実装 | RP2350 で hardware_rtc が廃止されたため。FatFS のファイル日時は 1980-01-01 固定 |
| 2026-05-22 | USB stdio を無効化（`pico_enable_stdio_usb 0`） | TinyUSB が SPI0/DMA と競合し SD マウント失敗するため |
| 2026-05-22 | ROM を Flash XIP で提供 | SD バンク読み込みが 7.5fps 止まりだったため。Flash XIP に切替後 ~60fps 達成 |
| 2026-05-23 | `kbd_init()` を `lcd_init()` より前に移動 | standalone 起動時、lcd_init の遅延中に STM32 が I2C 初期化する。SCL/SDA が浮動だと STM32 の I2C が正常起動しない |
| 2026-05-24 | LovyanGFX 移行。RGB565 + swap=true を採用 | RGB888 よりバッファ 1/3 小さく代入も軽い |
| 2026-05-24 | lgfx_config: invert=true 必須、rgb_order=false、setRotation(6) | ILI9488 パネルは自然反転あり → INVON で補正。setRotation(6) が正常ポートレート |
| 2026-05-24 | LovyanGFX 導入後に sleep_ms(1000) を追加 | LovyanGFX の初期化が ~20ms と短く SD マウントが間に合わなかったため |
| 2026-05-24 | I2S 化を断念・PWM 継続 | GP26/27 はアンプ直結 PWM 専用。ハードウェア的に I2S 不可 |
| 2026-05-24 | 音声 DMA IRQ 駆動に変更（ポーリング廃止） | DMA_BUF_SAMPLES を 548 に縮小し IRQ 駆動にすることで低レイテンシ（~33ms）を実現 |
| 2026-05-24 | TIMER_WRAP を 150MHz 対応値に修正 | RP2350 は 150MHz だが 125MHz 前提で計算していた。音程ズレ +2.4 半音を解消 |
| 2026-05-24 | PWM 解像度を 10bit → 12bit に拡張 | 量子化ノイズ改善（−60dB→−72dB）、転送量は変わらず無コスト |
| 2026-05-24 | LCD 描画と音声をともに Core 1 へ移行（後に音声 DMA IRQ は Core 0 へ再移行） | Core 0 を GB 実行専用にすることで干渉を解消。音声 DMA IRQ は multicore_lockout との干渉で Core 0 に戻した |
| 2026-05-25 | ILI9488 BGR 色並び順修正（rgb_order=false） | MADCTL=0x48 の bit3=BGR を見落としており R↔B が反転していた |
| 2026-05-25 | セーブデータを Flash に移行（SD 非依存化） | SD カード破損リスクを避けるため |
| 2026-05-25 | 30秒自動セーブを廃止 → ゲーム内セーブ後デバウンス保存に変更 | Flash 書き込みコストが高く毎回ロックアウトが発生するため不適 |
| 2026-05-25 | Flash 書き込み時に multicore_lockout を使用 | Core 1 は Flash 上のコードを実行しているため書き込み中の停止が必要 |
| 2026-05-25 | Flash レイアウト確定 | ROM 直後に SRAM 32KB、セーブステート 10スロット×32KB=320KB を配置 |
| 2026-05-25 | gb_core_consume_dirty() 追加でセーブ検出を修正 | is_dirty() はラッチのため毎フレーム判定に使うとカウントダウンが 0 に達しなかった |
| 2026-05-25 | SRAM セーブ検証をマジック + ROM タイトル照合に変更 | ブランクチェックでは別ビルドのゴミデータをすり抜けてゲームのセーブが不正表示になる問題を修正 |
| 2026-05-25 | 音声 DMA IRQ を Core 1 → Core 0 に移行 | Core 1 は multicore_lockout で停止するため Flash 書き込み中に IRQ が止まる。Core 0 では lockout の影響を受けない |
| 2026-05-25 | テンポずれを修正（TIMER_WRAP 4582→4578、AUDIO_SAMPLES 548→549） | int 切り捨てで毎フレーム 0.625 サンプル分が失われ 0.117% 遅かった。誤差を 0.004% に縮小 |
| 2026-05-25 | ESC でメニュー開く、閉じるは B ボタンのみ | Enter/ESC 両方を開閉に使うと誤操作が多かった |
| 2026-05-25 | g_menu_active フラグを追加 | ESC ハンドラ後に Core 1 がフレームを上書きする競合を防ぐ |
| 2026-05-25 | SD 操作の確認ダイアログとトーストを追加 | SD 復元は確認なしに上書きするリスクがあるため |
| 2026-05-25 | F1 スリープ（dormant）撤去 | F2 ステートセーブ後に電源断すれば同等の効果。powman 依存コード・SLEEP_MAGIC/RESUME_MAGIC・go_dormant() を全削除 |
| 2026-05-25 | ファンクションキー確定（F1=リセット / F2=セーブ / F3=ロード / F4=スロット） | スリープ撤去に伴いキー割り当てを整理 |
| 2026-05-25 | LCD バックライト制御断念 | STM32 ファームウェアが I2C 0x0A レジスタを未実装とみられる。メニューから削除 |
| 2026-05-25 | Enter キーを Start ボタンに追加 | 操作の直感性向上 |
| 2026-06-05 | minigb_apu を src/audio/ にローカルコピーして3件のバグを修正 | CH4 LFSR（左シフト→右シフト・出力ビット誤り）、CH1 sweep down（uint16_t *= -1 の UB）、CH1 sweep shift=0 誤 disable。コイン音が大幅改善。PicoCalc / AMOLED 両対応 |
| 2026-06-05 | CH4 出力を 25%（>> 2）にスケール | LFSR 修正後も CH4 が他チャンネルより目立ちすぎていたため。チャンネル消音テストで CH4 が原因と特定。PicoCalc / AMOLED 両対応 |
| 2026-06-05 | テストアプリ削除・ディレクトリ整理 | main.c→main_picocalc.c リネーム、amoled_sound_test/touch_test 削除、PicoCalc hw_config.c を src/boards/picocalc/ に移動（AMOLED と対称化）|
| 2026-06-05 | main_*.c の配置方針を決定 | 現状は CMake がボードごとに main_picocalc.c / main_amoled.c を選択するパターン A。ボードが増えた場合は「単一 main.c → board_run() を呼ぶ」パターン C（各ボードの実装を src/boards/<board>/app.c に収める）への移行を想定。現時点では 2 ボードのためパターン A を維持。|

---

## 調査済み情報

| トピック | 場所 |
|---|---|
| PicoCalc ハードウェア仕様（LCD・KB・SD・Audio）| `HardwareSpec.md` Part 1 |
| AMOLED ハードウェア仕様（QSPI・FT3168・ES8311・SD）| `HardwareSpec.md` Part 2 |
| 開発環境構築手順 | `DevelopmentEnvironment.md` |
| プロジェクト全体仕様・方針 | `Spec.md` |
| 移植で得た非自明な知見 | `PORTING.md` |

---

---

# AMOLED ターゲット進捗

対象デバイス: **Waveshare RP2350-Touch-AMOLED-1.8**

---

## 現在のフォーカス

**✅ AMOLED ポーティング完了（2026-06-05）**

---

## マイルストーン進捗

### Milestone A1: QSPI AMOLED 表示ドライバ ✅ 完了

- [x] QSPI PIO ドライバ実装（`src/drivers/display/qspi_pio.c`）
- [x] AMOLED 初期化シーケンス（`src/drivers/display/amoled_1in8.c`）
- [x] DMA 転送（`amoled_1in8_display_window()`）
- [x] GB 2× スケール表示（320×288 を 368×448 パネル中央に配置）実機確認済み

### Milestone A2: タッチ入力確認 ✅ 完了

- [x] FT3168 ドライバ（`src/drivers/input/ft3168_touch.c`）
- [x] ポイントモード（座標取得）実機確認済み
- [x] ジェスチャーモード（UP/DOWN/LEFT/RIGHT/CLICK/DOUBLE_CLICK）実機確認済み
- [x] **1点タッチのみ対応を実機確認**（2点目は応答なし）
- [x] D-Pad / ボタン 4象限 / ステータスバーのタッチゾーン設計

### Milestone A3: ES8311 I2S 音声確認（Phase 2b）✅ 完了

- [x] ES8311 I2C 初期化（`src/drivers/audio/es8311.c`）
- [x] PIO MCLK 生成（`audio_i2s_pio.h`、pio1 SM0）
- [x] PIO I2S 出力（pio2 SM0、ES8311 マスターモード）
- [x] DMA IRQ 駆動ダブルバッファ（Core 0、DMA_IRQ_0）
- [x] 440Hz サイン波テストで音声出力確認（chip ID: 0x1183）

### Milestone A4: GB エミュレーション統合 ✅ 完了

- [x] `main_amoled.c` — GB コア + 表示 + タッチ + 音声の統合
- [x] ダブルフレームバッファ（Core 0 書込 / Core 1 転送）
- [x] SPSC リングバッファ（4096 サンプル / 128ms）
- [x] Bresenham レートコレクション（785/1024 / フレーム）
- [x] APU 音声出力（32000Hz ES8311）実機確認済み

### Milestone A5: セーブ対応 ✅ 完了

- [x] SRAM 自動セーブ（1秒デバウンス、Flash 書込、マルチコアロックアウト）
- [x] セーブステート（10スロット、Flash）
- [x] ステータスバーのタッチでセーブ / スロット切替 / ロード

### Milestone A6: SD カード / ROM 管理 ✅ 完了

- [x] SD カード（SPI1 / GP26-28、hw_config.c）
- [x] SD からの ROM 書き込み → Flash XIP 起動
- [x] picotool で直接 Flash に書き込む方法も対応

### Milestone A7: 200MHz オーバークロック ✅ 完了（2026-06-03）

### Milestone A8: コードクリーンアップ ✅ 完了（2026-06-05）

- [x] `set_sys_clock_khz(200000, true)` 適用
- [x] QSPI PIO div を動的計算（`clock_get_hz(clk_sys) / (75MHz × 2)`）で速度固定
- [x] フレームバジェット余裕: ~16.76ms → **~12.6ms**（余裕 4ms）
- [x] 重い場面でのスローダウン解消を実機確認

---

## 保留・今後の課題

| 項目 | 内容 |
|------|------|
| 音量調整 UI | 現状 `es8311_set_volume(60)` 固定。メニューへの組み込みが必要 |
| 2点タッチ | FT3168 の制限（1点のみ）。POWER 物理ボタン（GPIO18）を A ボタンとして補完中 |
| パレット切替 | PicoCalc ではメニューから変更可能。AMOLED では未実装 |
| メニュー UI | AMOLED ではタッチ UI 未実装（ステータスバーのみ） |
| POWER ボタン長押し（バッテリー使用時） | **USB 給電中は安全**（AXP2101 が VBUS 検出で電源 OFF を無効化）。バッテリー使用時は長押し（約 4 秒）で AXP2101 が電源 OFF を実行する可能性あり。長押し検出ロジックの追加が必要。`buttons.md` 参照 |

---

## 決定事項ログ（AMOLED）

| 日付 | 決定内容 | 理由 |
|------|---------|------|
| 2026-06-03 | サンプリングレート 32000Hz | ES8311 係数テーブルに存在する最近似値 |
| 2026-06-03 | Bresenham 補正 (785/1024) | 32000 × 70224 / 4194304 = 535 + 785/1024 の厳密解 |
| 2026-06-03 | DMA バッファ 1024 samples（32ms） | 512 samples（16ms）では引っかかりが多かったため |
| 2026-06-03 | リングバッファ 4096 samples（128ms） | 重い場面でのバッファ枯渇を抑止 |
| 2026-06-03 | 音量 60 | 80 では大きすぎた |
| 2026-06-03 | 200MHz オーバークロック | APU 処理でフレームバジェットがギリギリだったため |
| 2026-06-03 | QSPI div 動的計算 | div=1.0 固定では 200MHz 時に 100MHz QSPI となり表示不可になった |
| 2026-06-03 | POWER ボタン（GPIO18）= A ボタン | FT3168 が 1点タッチのみのため方向 + A の同時入力がタッチ単独では不可。USB 給電限定で AXP2101 の電源 OFF 無効を利用し短押し・長押し問わず A として扱う。バッテリー使用時は要再検討（`buttons.md` 参照） |
