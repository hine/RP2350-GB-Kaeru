# CLAUDE.md

RP2350 系デバイス向け Game Boy エミュレータ（「カエルの為に鐘は鳴る」専用）。  
対応ターゲット: **PicoCalc**（ClockworkPi PicoCalc）/ **AMOLED**（Waveshare RP2350-Touch-AMOLED-1.8）

---

## ドキュメントの場所

| ファイル | 場所 |
|---------|------|
| PROGRESS.md | プロジェクトルート（`../RP2350-GB-Kaeru-notes/PROGRESS.md` へのシンボリックリンク） |
| SCRATCH.md | プロジェクトルート（`../RP2350-GB-Kaeru-notes/SCRATCH.md` へのシンボリックリンク） |

実体は `private-docs` orphan ブランチ（worktree: `../RP2350-GB-Kaeru-notes/`）で管理。

セッション開始時は必ず両ファイルを読むこと。

PROGRESS.md・SCRATCH.md への変更は `git -C ../RP2350-GB-Kaeru-notes` でコミットし、
`git -C ../RP2350-GB-Kaeru-notes push origin private-docs` で push する。

---

## Claude との作業ルール

### 問題発生時のドキュメントファースト

1. **実行前に** `SCRATCH.md` へ問題・仮説・対策を記載する
2. 試行のたびに結果を `SCRATCH.md` の試行ログへ追記する（ユーザーが OK と言うまで続ける）
3. 解決したら要点を `PROGRESS.md` の決定事項ログに転記し、`SCRATCH.md` のセクションを削除する

### コミット頻度

- **1機能 = 1コミット**を原則とする。セッション終了まで溜め込まない
- 目安: ビルドが通って動作確認できた段階でコミット
- バグ修正・リファクタ・ドキュメント更新はそれぞれ別コミットにする
- コミットメッセージは Conventional Commits 形式（`feat:` / `fix:` / `refactor:` / `docs:` 等）、内容は日本語で

### ファイル作成前の確認事項

- `git log --diff-filter=A -- <ファイル名>` でそのファイルの初回コミットを確認する
- このリポジトリの md ファイルは全て本プロジェクト内で作成されたもの（フォーク元由来ではない）

---

## リポジトリ構成

| リモート | GitHub リポジトリ | 可視性 |
|---------|----------------|------|
| `origin` | `RP2350-GB-Kaeru-dev` | プライベート |
| `public` | `RP2350-GB-Kaeru` | パブリック |

**ブランチ構成：**

| ブランチ | 用途 |
|---------|------|
| `main` | 常にリリース可能な状態。タグはここに打つ |
| `dev` | 通常の開発ブランチ。`main` へは PR でマージ |
| `board/<name>` | 新デバイス移植作業 |
| `fix/<name>` | バグ修正 |
| `private-docs` | PROGRESS.md・SCRATCH.md 専用（orphan）。`origin` にのみ push |

**リリース手順：**

```bash
git checkout main
git merge dev
git tag -a v1.x.x -m "..."
git push origin main dev
git push public main
git push public v1.x.x
```

**新PC・新環境でのセットアップ：**

```bash
# 1. プライベートリポジトリをクローン
git clone --recurse-submodules <private-repo-url> RP2350-GB-Kaeru-dev
cd RP2350-GB-Kaeru-dev

# 2. パブリックリモートを追加
git remote add public <public-repo-url>

# 3. private-docs worktree をセットアップ
git fetch origin private-docs
git worktree add ../RP2350-GB-Kaeru-notes private-docs

# 4. シンボリックリンクを作成
ln -s ../RP2350-GB-Kaeru-notes/PROGRESS.md PROGRESS.md
ln -s ../RP2350-GB-Kaeru-notes/SCRATCH.md SCRATCH.md

# 4. ビルド環境構築は DevelopmentEnvironment.md を参照
```
