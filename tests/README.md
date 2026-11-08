# 回帰テスト（P1・P2）

## Qt による画面・移動・探索の検証

Qt 6 の Qt Test と MSVC の開発環境を使用する。Debug 構成では `_bin/incrementalsearchplugind.dll`、Release 構成では `_bin/incrementalsearchplugin.dll` が必要になる。対応する Qt とプラグインの依存 DLL が `PATH` から見つかるようにする。vcpkg を使用する環境では、該当構成の `bin` も追加する。

検索テストが最新の実装を使用するよう、先に IncrementalSearchPlugin をビルドする。リポジトリのルートで、MSVC の開発者用コマンドプロンプトから実行する。

```bat
mkdir build\search-plugin
cd build\search-plugin
qmake ..\..\subprojects\IncrementalSearchPlugin\IncrementalSearchPlugin.pro CONFIG+=debug
jom
cd ..\..
mkdir build\p1-tests
cd build\p1-tests
qmake ..\..\Tests.pro CONFIG+=debug
jom
cd ..\..
set QT_FORCE_STDERR_LOGGING=1
_bin\TreeWalkerTests.exe -platform offscreen
```

- `p1_regression.cpp` は本体のコードを使い、検索中の再構築、起動対象、無効な移動先での状態維持、通常フォルダ・ルートへの移動を検証する。P1-08 では、詳細・サムネイルの両表示でツリー検索が右ペインへ適用されないこと、ペイン間の検索切替、右ペインの表示モード切替後の検索継続、F5 後の検索・複数選択・件数の維持を確認する。取得待ち中の入力変更・解除・別フォルダへの移動は、結果の通知を入力後にキューで届けるケースも使って検証する。外部アプリの起動は URL ハンドラーで捕捉する。
- `network_discovery_tests.cpp` は列挙用バックエンドを注入し、列挙が停止した場合の破棄時間、停止後の通知、再開時の状態分離を検証する。
- `model_notifications_tests.cpp` は `QAbstractItemModelTester` を使用し、挿入・削除・再接続・検索切替と、詳細／サムネイルのリセット通知を検証する。検索中の子の追加・削除が所有権を変えずに拒否され、一括削除も停止することを確認する。項目とフィルターの一括更新は、0 件一致を含めてリセット通知が 1 組で、検索解除後に全件を復元できることを確認する。
- `address_input_tests.cpp` は実際のキー入力でアドレスの文字入力・編集・貼り付け、`Alt+D` / `Ctrl+L`、表示切替とフォーカス移動を検証する。隠しファイル切替は一時フォルダを使い、ツリー検索中の選択・展開状態の維持、0 件一致からの復帰、隠しフォルダ内から表示をオフにした場合の祖先への移動と、右ペインの件数を確認する。検索入力では ASCII、Unicode、空入力と既存の削除用入力を検証し、編集欄への Unicode 入力も確認する。貼り付けに使用したクリップボードは元の内容へ復元する。
- `special_locations_tests.cpp` は一時 JSON を使用し、ブックマークの切替・空フォルダ・同名フォルダ・URL 起動・F5 再読込、My Computer の階層保持、パスのない Shell 行の PIDL による起動を検証する。HTTP / HTTPS の起動はハンドラーで捕捉し、ブラウザーは起動しない。
- `startup_settings_tests.cpp` は一時 INI を使用し、保存値の再読み込み、欠損ファイル・キーの既定値、設定画面との一致、キャンセル時の設定保持を検証する。ユーザーの設定ファイルは使用しない。
- `reading_search_tests.cpp` はプラグインに組み込まれた実際の MeCab 辞書を使用し、形態素をまたぐ読みの検索と、元文字列の復元・強調範囲を検証する。P2-06 の `nn` 変換は現行仕様を維持する。
- `com_workers_tests.cpp` は Windows の実際の COM を使用し、初期化の入れ子と失敗、暗黙 MTA、スレッドごとの Shell 所有と解放、一覧・実画像のサムネイル取得、待機中の STA メッセージ処理を検証する。
- 正常移動のテストは実行ディレクトリ内に一時フォルダを作り、終了時に削除する。非表示フォルダ配下ではなく、書き込み可能な通常のディレクトリから実行する。

## Shell の戻り値と解放の検証

Windows の MinGW g++ と Python 3 を使用する。

```powershell
python tests/empty_shell_read_permission.py --compiler C:/Qt/Tools/mingw1310_64/bin/g++.exe
```

本体の `WindowsFileSystemProvider::hasReadPermission()` を実行時に抽出し、SDK の偽 `IShellFolder` と `IEnumIDList` に接続する。`S_FALSE / NULL`、空の列挙子、アクセス拒否など7ケースを確認する。生成するソースと実行ファイルは一時ディレクトリに置き、自動削除する。
