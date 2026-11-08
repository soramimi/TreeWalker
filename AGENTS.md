# 開発エージェント向けガイド

## このリポジトリについて

TreeWalker は C++17 / Qt Widgets で開発するデスクトップ向けファイルマネージャー。ビルドには qmake を使う。
概要は [README.md](README.md)、設計方針は [CONCEPT.md](CONCEPT.md)、回帰テストの実行手順は [tests/README.md](tests/README.md) を参照する。
このガイドはリポジトリ全体に適用する。作業対象のディレクトリに適用される別の `AGENTS.md` がある場合は併せて確認する。`subprojects/IncrementalSearchPlugin/dicedit/` には専用のガイドがある。

## 設計方針

- 左にディレクトリツリー、右に現在の場所の内容を表示し、実際のファイルシステムを分かりやすく扱う。
- ファイルとディレクトリを区別する。ZIP などのアーカイブを透過的な仮想フォルダにしない。
- ファイル名の拡張子を隠さない。強調表示は既存の設定に従う。
- 右ペインの表示は詳細表示とサムネイル表示の2種類を基本とする。
- キーボード操作と、日本語ファイル名をローマ字で絞り込むインクリメンタル検索を重視する。編集欄への通常入力をナビゲーション用のキー処理で奪わない。
- ローマ字の `nn` 変換はユーザーが指定した現行仕様を維持する。たとえば `nn` は `ン`、`konna` は `コンア` になる。一般的なローマ字入力の期待だけを理由に変換規則を変更しない。

## 主な構成

| 場所 | 役割 |
| --- | --- |
| `TreeWalker.pro` / `TreeWalker.pri` / `common.pri` | 本体のビルド、共通ソース一覧、コンパイラー・依存ライブラリの設定 |
| `src/main.cpp` / `ApplicationGlobal.*` | 起動、プラグインの読み込み、共有状態 |
| `src/MainWindow.*` | 画面操作、移動、検索、特殊ロケーションの振り分け |
| `src/FolderTreeView.*` / `FileItemModel.*` | ツリーとファイル一覧のモデル |
| `src/FileTableView.*` / `ThumbnailView.*` | 詳細表示とサムネイル表示 |
| `src/AbstractFileSystemProvider.*` / `BasicFileSystemProvider.*` / `WindowsFileSystemProvider.*` | ファイルシステムの抽象化と各環境の実装 |
| `src/FetchLocationThread.*` / `ThumbnailLoader.*` / `NetworkDiscoveryThread.*` | 一覧、サムネイル、ネットワーク探索のバックグラウンド処理 |
| `src/WindowsCom.h` / `WindowsShellAPI.*` / `ItemIdList.*` | Windows COM、Shell、ロケーション識別子 |
| `src/SettingsDialog.*` / `ApplicationSettings.*` / `MySettings.*` | 設定画面、設定値、INI の読み書き |
| `subprojects/IncrementalSearchPlugin/` | MeCab と組み込み辞書による読みの生成・検索 |
| `subprojects/FileTypePlugin/` | file / oniguruma を利用するファイル種別判定 |
| `Tests.pro` / `tests/` | 本体のソースを利用する Qt Test と、Shell の戻り値を検証する独立テスト |

## ビルド

通常は Qt 6 と C++17 対応コンパイラーを使い、`build/` 配下の別ディレクトリでビルドする。Windows の例は、Qt の `qmake` と `jom` が使える MSVC 開発者用コマンドプロンプトで、リポジトリのルートから実行する。

```bat
mkdir build\app
cd build\app
qmake ..\..\TreeWalker.pro CONFIG+=debug
jom debug
```

本体とプラグインは別々の qmake プロジェクトであり、本体のビルドだけではプラグインは更新されない。次の各 `.pro` をそれぞれ専用のビルドディレクトリでビルドする。

- `subprojects/IncrementalSearchPlugin/IncrementalSearchPlugin.pro`
- `subprojects/FileTypePlugin/FileTypePlugin.pro`

Windows では本体とプラグインを `_bin/` へ出力する。Debug の本体は `TreeWalkerd.exe`、Release は `TreeWalker.exe`。プラグインは Debug のみ名前の末尾に `d` が付く。
本体の起動には `filetypeplugin` と `incrementalsearchplugin` の両方が必要になる。Qt・コンパイラー・アーキテクチャ・Debug / Release の構成を揃え、Qt と依存ライブラリの DLL を実行時に見つけられるようにする。
非 Windows 環境ではプラグインを `lib/soramimi.jp/` へ出力し、ビルドには環境に合った `make` などを使用する。

MSVC の依存ライブラリのパスは `common.pri` で `C:/vcpkg/installed/x64-windows` を参照している。検索プラグインは zstd、ファイル種別プラグインは zlib / zstd と file / oniguruma の静的ライブラリを使用する。
ファイル種別プラグインの静的ライブラリがない場合は、同ディレクトリの `libfile.pro` と `liboniguruma.pro` の構成も確認する。環境差への対応では、既存のビルド設定を確認してから変更する。

## 検証

- 手順と前提条件は `tests/README.md` に従う。現在のテストプロジェクトは `Tests.pro`、Windows の実行ファイルは `_bin/TreeWalkerTests.exe`。
- 検索プラグインを変更した場合は、テストと同じ構成でプラグインを先に再ビルドする。古い DLL で新しいソースを検証したことにしない。
- Qt Test は `TreeWalkerTests.exe -platform offscreen` で実行できる。必要に応じて `QT_FORCE_STDERR_LOGGING=1` を設定する。
- ツリーモデルの変更では `tests/model_notifications_tests.cpp`、入力では `tests/address_input_tests.cpp`、特殊ロケーションでは `tests/special_locations_tests.cpp` を確認する。
- 設定では `tests/startup_settings_tests.cpp`、日本語検索では `tests/reading_search_tests.cpp`、Windows の COM やワーカーでは `tests/com_workers_tests.cpp` / `tests/network_discovery_tests.cpp` を確認する。
- `WindowsFileSystemProvider::hasReadPermission()` の戻り値・解放処理には `tests/empty_shell_read_permission.py` を使用する。Python 3 と Windows の MinGW g++ が必要になる。
- テストには一時ファイルや制御可能なバックエンドを使い、ユーザーの INI や Chrome プロファイルへ依存させない。外部アプリの起動はハンドラーで捕捉する。
- 変更に応じたビルド・テストを行い、確認した構成と結果を報告する。Windows 専用コードは Windows で検証し、未検証の環境を区別する。

## 実装時の注意

- 周辺の命名、タブによるインデント、括弧の配置に合わせる。UTF-8 と既存の改行形式を尊重し、無関係な整形変更を避ける。
- Qt Designer の変更は `.ui` を編集する。`ui_*.h`、`moc_*.cpp`、Makefile などのビルド生成物を直接修正しない。ソースを追加した場合は該当する `.pro` / `.pri` にも登録する。
- Qt のモデルは変更前に `beginInsertRows` / `beginRemoveRows` などで通知し、実データを更新してから対応する終了通知を行う。リセットを重複させず、必要に応じて `QAbstractItemModelTester` で契約を確認する。
- フィルター解除やモデル再構築の前に、起動対象のパス・URL・種別・識別子を値として確保する。破棄済みのツリー項目や古い `QModelIndex` を参照しない。
- ワーカーから GUI のオブジェクトを参照しない。終了時の所有権と停止要求を明確にし、古い一覧取得結果が現在の表示を上書きしないようにする。
- Windows の COM 初期化と終了は `WindowsComApartment` で対応させる。Shell インターフェースは `WindowsShellAPI::forCurrentThread()` などで使用スレッドごとに所有し、STA のメッセージ処理と解放順を保つ。
- 実フォルダ、パスのない Shell 項目、ブックマークなどの種別を区別する。仮想項目を通常のファイル列挙へ渡さない。移動先の解決に失敗した場合は現在の表示を維持する。
- 設定の読み書きは既存の共通処理を使う。起動時は設定パス確定後、主画面生成前に読み込み、設定画面のキャンセルでは実行中の値や保存内容を変更しない。
- 読みの一致範囲は元文字列へ対応付け、重複する一致を統合する。強調表示の断片を連結した結果が元文字列と一致することを保つ。
- 同梱の MeCab / file / oniguruma などの外部由来コードと、大きな辞書生成配列は、対象の変更に必要な場合だけ編集する。
- 作業開始時に差分を確認し、ユーザーの既存変更を維持する。`build/`、`_bin/`、個人の IDE 設定などを成果物へ含めない。
