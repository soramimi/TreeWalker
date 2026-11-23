# インクリメンタル検索プラグインのテスト

Qt Test で、組み込み MeCab 辞書による読みの照合、元の文字列上の一致範囲、ローマ字変換を確認する。先に検索プラグインを同じ Qt・コンパイラー・ビルド構成でビルドする。

Linux の Release 構成では、リポジトリのルートから実行する。

```bash
mkdir -p build/search-tests
cd build/search-tests
qmake6 ../../subprojects/IncrementalSearchPlugin/tests/IncrementalSearchTests.pro CONFIG+=release
make -j4
cd ../..
_bin/IncrementalSearchTests
```

Windows では同じプロジェクトを `qmake` と `jom` でビルドし、`_bin/IncrementalSearchTests.exe` を実行する。Qt とプラグインの依存 DLL を `PATH` から見つけられるようにする。Debug の検索プラグインを使う場合はテストも `CONFIG+=debug` でビルドする。

Qt Widgets の描画とファイル一覧での検索は本体の `TreeWalkerTests` で確認する。
