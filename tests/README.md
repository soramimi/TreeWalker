# 回帰テスト（P1・P2・P3）

## Qt による画面・移動・探索の検証

Qt 6 の Qt Test と MSVC の開発環境を使用する。Debug 構成では `_bin/incrementalsearchplugind.dll` と `_bin/filetypeplugind.dll`、Release 構成では末尾の `d` がない両プラグインが必要になる。対応する Qt とプラグインの依存 DLL が `PATH` から見つかるようにする。vcpkg を使用する環境では、該当構成の `bin` も追加する。

テストが最新の実装を使用するよう、先に両プラグインをビルドする。FileTypePlugin は、対応する file / oniguruma の静的ライブラリと生成済み magic 辞書を用意しておく（`AGENTS.md` と `subprojects/FileTypePlugin/` のビルド設定を参照）。リポジトリのルートで、MSVC の開発者用コマンドプロンプトから実行する。
検索プラグインのインターフェースは 2.0。読みキャッシュ API の追加前の DLL / SO は新しい本体・テストから使用できないため、同じ構成で再ビルドする。

```bat
mkdir build\search-plugin
cd build\search-plugin
qmake ..\..\subprojects\IncrementalSearchPlugin\IncrementalSearchPlugin.pro CONFIG+=debug
jom
cd ..\..
mkdir build\filetype-plugin
cd build\filetype-plugin
qmake ..\..\subprojects\FileTypePlugin\FileTypePlugin.pro CONFIG+=debug
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
- `model_notifications_tests.cpp` は `QAbstractItemModelTester` を使用し、挿入・削除・再接続・検索切替と、詳細／サムネイルのリセット通知を検証する。検索中の子の追加・削除が所有権を変えずに拒否され、一括削除も停止することを確認する。項目とフィルターの一括更新は、0 件一致を含めてリセット通知が 1 組で、検索解除後に全件を復元できることを確認する。照合回数も数え、0 件一致が繰り返し参照で再計算されず、変更後だけ再計算されることを検証する。
- `address_input_tests.cpp` は実際のキー入力でアドレスの文字入力・編集・貼り付け、`Alt+D` / `Ctrl+L`、表示切替とフォーカス移動を検証する。隠しファイル切替は一時フォルダを使い、ツリー検索中の選択・展開状態の維持、0 件一致からの復帰、隠しフォルダ内から表示をオフにした場合の祖先への移動と、右ペインの件数を確認する。検索入力では ASCII、Unicode、空入力と既存の削除用入力を検証し、編集欄への Unicode 入力も確認する。貼り付けに使用したクリップボードは元の内容へ復元する。
- `special_locations_tests.cpp` は一時 JSON を使用し、ブックマークの切替・空フォルダ・同名フォルダ・URL 起動・F5 再読込、My Computer の階層保持、パスのない Shell 行の PIDL による起動を検証する。HTTP / HTTPS の起動はハンドラーで捕捉し、ブラウザーは起動しない。
- `startup_settings_tests.cpp` は一時 INI を使用し、保存値の再読み込み、欠損ファイル・キーの既定値、設定画面との一致、キャンセル時の設定保持を検証する。ユーザーの設定ファイルは使用しない。
- `reading_search_tests.cpp` はプラグインに組み込まれた実際の MeCab 辞書を使用し、形態素をまたぐ読みの検索と、元文字列の復元・強調範囲を従来 API と準備済み API の両方で検証する。モデルのフィルター更新・詳細とサムネイルの描画で読み生成回数が増えないこと、名前変更後の更新、拡張子分割・長い名前・Unicode を確認する。1,000 件の名前の準備時間と、検索のキャッシュ有無による時間も参考値として出力し、時間の閾値では合否を決めない。P2-06 の `nn` 変換は現行仕様を維持する。
- `thumbnail_loader_tests.cpp` は制御可能なデコーダーとセマフォで、成功・失敗のキャッシュ、同一パスの重複防止、処理中の取り消し・再要求・キャッシュ消去・メタデータ変更、失敗結果の追い出し、停止・再開を検証する。固定時間の sleep で完了順を仮定しない。実画像と非画像、失敗後に画像へ変わるケースも確認する。`p1_regression.cpp` では実際の F5 入力からサムネイルが更新されることも検証する。
- `worker_shutdown_tests.cpp` は停止しない一覧取得・画像取得をセマフォで制御し、通知元の破棄後の完了、再開時の状態分離、スレッドローカル解放中の待機上限を確認する。子プロセスでは一覧取得 4 件とサムネイル 8 件を止めたまま、終了要求から 3 秒以内に正常終了することを確認する。`p1_regression.cpp` は主画面の破棄が共通の 1 秒の待機期限を使うこと、破棄後の遅い成功・失敗、A → B → A の古い結果を検証する。正常時の順序はセマフォで制御し、退行時のハングを防ぐ watchdog は別に設ける。
- `plugin_loading_tests.cpp` は一時パスによるファイル欠落・不正ライブラリ、テスト用ファクトリーによる型不一致・生成失敗・初期化失敗、生成物の解放を検証する。実際のファイル種別プラグインも初期化し、子プロセスでエラーダイアログの内容と非ゼロ終了を確認する。利用中のプラグインは移動・破損させない。
- `com_workers_tests.cpp` は Windows の実際の COM を使用し、初期化の入れ子と失敗、暗黙 MTA、スレッドごとの Shell 所有と解放、一覧・実画像のサムネイル取得、待機中の STA メッセージ処理を検証する。
- 一覧とサムネイルの完了・失敗 signal は GUI スレッドから届く。COM の状態は通知先ではなく実際のバックエンド内で計測し、終了時は TLS / COM の解放まで含めた完了を確認する。
- 正常移動のテストは実行ディレクトリ内に一時フォルダを作り、終了時に削除する。非表示フォルダ配下ではなく、書き込み可能な通常のディレクトリから実行する。

## 非同期列挙とファイル操作の検証

ツリーと右ペインの列挙は非同期で、両方を合わせて同時実行は最大4件。移動先や子ノードの確認には `QTRY_COMPARE` / `QTRY_VERIFY` を使い、GUI のイベント処理を止めて列挙完了を待たない。`p1_regression.cpp` はツリー取得中のイベント処理、古い世代の結果の破棄、プロバイダー複製、名前変更・フォルダー作成の入力検証と衝突も確認する。`worker_shutdown_tests.cpp` は停止しない列挙がある場合の実行数上限と、開始前に取り消された要求が実行されないことを検証する。

Windows の隠し属性を持つ実ファイル・フォルダーの情報取得は `com_workers_tests.cpp` で検証する。Linux の実行結果にはこの Windows 専用検証を含めない。

## Linux での AddressSanitizer による確認

同じ Qt / Debug 構成の両プラグインを先にビルドする。以下は本体のソースとテストを ASan でビルドする例で、プラグイン自体は通常の Debug バイナリーを使う。

```bash
mkdir -p build/asan-tests
cd build/asan-tests
qmake6 ../../Tests.pro CONFIG+=debug \
  'QMAKE_CXXFLAGS+=-fsanitize=address -fno-omit-frame-pointer' \
  'QMAKE_LFLAGS+=-fsanitize=address' -after TARGET=TreeWalkerTestsAsan
make -j4
cd ../..
ASAN_OPTIONS=detect_leaks=0 _bin/TreeWalkerTestsAsan -platform offscreen
```

`-after` で出力名を変更し、通常のテスト実行ファイルを上書きしない。ここではリーク検出を無効にし、解放後アクセス、配列の確保・解放の不一致などを確認する。停止中のバックエンドを持つ子プロセスでも同じ環境変数を引き継ぐ。

## Shell の戻り値と解放の検証

Windows の MinGW g++ と Python 3 を使用する。

```powershell
python tests/empty_shell_read_permission.py --compiler C:/Qt/Tools/mingw1310_64/bin/g++.exe
```

本体の `WindowsFileSystemProvider::hasReadPermission()` を実行時に抽出し、SDK の偽 `IShellFolder` と `IEnumIDList` に接続する。`S_FALSE / NULL`、空の列挙子、アクセス拒否など7ケースを確認する。生成するソースと実行ファイルは一時ディレクトリに置き、自動削除する。

## コピー・移動・クリップボード・ドラッグ＆ドロップ

共通バックエンドと Qt のドロップ確認は、本体プラグインに依存しない専用テストでも検証する。

```bash
mkdir -p build/transfer-tests
cd build/transfer-tests
qmake6 ../../tests/TransferTests.pro
make -j4
./TransferTests -platform offscreen
```

`transfer_tests.cpp` はコピー、移動、再帰フォルダー、競合のスキップ／別名／置換、途中キャンセル、競合待ち中のキャンセル、元や転送先の途中変更、リンク循環、権限エラー、部分失敗、MIME 形式を検証する。Linux の `/dev/shm` が別ファイルシステムで書き込み可能なら、実際の別ファイルシステム間の移動も実行する。利用できない場合はスキップとして報告する。

ドラッグ確認は Move / Copy / Cancel / Esc / 閉じる操作を検証する。3種類のビューにネイティブ Qt イベントを送り、修飾キーなし／Ctrl／Shift のすべてで確認を経ることを確認する。外部ソースが選択した操作を許可しない場合も、確認後に拒否してファイルを変更しないことを検証する。

本体の `p1_regression.cpp` は3ビュー×コピー／移動の6ケース、転送先一覧と選択の更新、別の場所へ移動した場合の表示維持、閲覧フォルダー自体が移動した場合の追従、クリップボードの途中差し替え、部分移動後の切り取り対象の保持を確認する。

専用テストの ASan 実行例：

```bash
mkdir -p build/transfer-asan
cd build/transfer-asan
qmake6 ../../tests/TransferTests.pro CONFIG+=debug \
  'QMAKE_CXXFLAGS+=-fsanitize=address -fno-omit-frame-pointer' \
  'QMAKE_LFLAGS+=-fsanitize=address' -after TARGET=TransferTestsAsan
make -j4
ASAN_OPTIONS=detect_leaks=0 ./TransferTestsAsan -platform offscreen
```

### Thunar との X11 統合テスト

Qt の xcb プラグイン、Thunar、xdotool、Xvfb、xauth、dbus-run-session、dbus-update-activation-environment と Python 3 が必要。ユーザーの表示やクリップボードに影響しない独立した X11／D-Bus セッションで実行する。

```bash
xvfb-run -a -s '-screen 0 1200x900x24' dbus-run-session -- \
  python3 tests/external_transfer_x11.py \
  --runner build/transfer-tests/TransferTests --drag
```

一時ディレクトリと独立した XDG 設定を使用する。双方向クリップボードのコピー／移動4ケース、Thunar からの実ドラッグの Copy（Ctrl）、Move（修飾なし）、Cancel（Shift）の3ケースを検証する。固定の Thunar レイアウトを使うため、UI 構成が変わった場合はスクリプトの座標とウィンドウ検出を確認する。外部へのドラッグは安全な確認を保証できないため未対応。

### Windows で必要な実機検証（未実行）

- Explorer → TreeWalker と TreeWalker → Explorer の Ctrl+C／X／V を、それぞれコピー・移動・繰り返し貼り付けで確認する。
- 3ビューへの Explorer からのドロップで Move / Copy / Cancel が毎回表示されることを、Ctrl／Shift の有無と同一／別ボリュームで確認する。
- 相手が CopyAction による安全な応答を許可しない場合は、確認後に拒否され、元も転送先も変更されないことを確認する。
- 既存ファイル／フォルダーとの競合、読み取り専用、隠し属性、使用中ファイル、長いパス、日本語名、別ボリュームでのキャンセル・容量不足・部分失敗を確認する。
- シンボリックリンクをたどらずに扱うこと、非対応のジャンクション等を拒否すること、移動元を二重削除しないことを確認する。
- TreeWalker から Explorer へのドラッグではファイル URL が公開されず、ファイルが転送されないことを確認する。

Linux の結果を Windows の検証成功として扱わない。
