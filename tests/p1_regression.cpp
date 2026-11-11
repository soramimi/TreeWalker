#include <QApplication>
#include <QElapsedTimer>
#include <QAbstractItemModelTester>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QKeyEvent>
#include <QPersistentModelIndex>
#include <QSignalBlocker>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <atomic>

#include "ApplicationGlobal.h"
#include "BasicFileSystemProvider.h"
#include "FileTableView.h"
#include "FolderTreeView.h"
#include "LoadPlugin.h"
#include "MainWindow.h"
#include "StatusLabel.h"
#include "RenameDialog.h"
#include <QLineEdit>
#include <QClipboard>
#include <QMessageBox>
#include <QAbstractButton>
#include "TransferClipboard.h"
#include "worker_test_support.h"
#include "ThumbnailView.h"
#include "subprojects/IncrementalSearchPlugin/src/IncrementalSearchInterface.h"
#ifdef Q_OS_WIN
#include "WindowsCom.h"
#include "WindowsShellAPI.h"
#endif

ApplicationGlobal *global = nullptr;

int runFileOpenTests(int argc, char **argv);
int runModelNotificationTests(int argc, char **argv);
int runAddressInputTests(int argc, char **argv);
int runSpecialLocationTests(int argc, char **argv);
int runStartupSettingsTests(int argc, char **argv);
int runReadingSearchTests(int argc, char **argv);
int runThumbnailLoaderTests(int argc, char **argv);
int runWorkerShutdownTests(int argc, char **argv);
int blockedWorkersChild();
int runPluginLoadingTests(int argc, char **argv);
int pluginErrorChild(QString const &path);

#ifdef Q_OS_WIN
int runNetworkDiscoveryTests(int argc, char **argv);
int runComWorkerTests(int argc, char **argv);
#endif

class P1RegressionTest : public QObject {
	Q_OBJECT
private:
	QList<QUrl> opened_urls_;

	bool prepareWindow(MainWindow &window)
	{
		global->mainwindow = &window;
		// Tree rebuilds must not depend on the user's installed Chrome profile.
		window.fetchBookmarks(global->app_config_dir + "/empty-test-bookmarks.json");
		auto *table = window.findChild<FileTableView *>("tableView");
		auto *thumbnails = window.findChild<ThumbnailView *>("thumbnailView");
		if (!table || !thumbnails) return false;
		table->setModel(window.fileitemmodel());
		thumbnails->setModel(window.fileitemmodel());
		table->setKind(Kind::Directory);
		thumbnails->setKind(Kind::Directory);
		return true;
	}

	static bool createFile(QString const &path)
	{
		QFile file(path);
		return file.open(QIODevice::WriteOnly) && file.write("test\n") == 5;
	}

	static void populateFiles(MainWindow &window, QString const &directory)
	{
		auto *model = window.fileitemmodel();
		model->beginResetModel();
		model->clearItems();
		model->setKind(Kind::Directory);
		for (QString const &name : {QString("a.txt"), QString("b.txt")}) {
			FileItemModel::Item item;
			item.name = name;
			item.path = directory + '/' + name;
			item.info.name = name;
			item.info.path = item.path;
			item.info.iidl = ItemIdList(item.path);
			item.size = item.info.size = 5;
			model->addItem(std::move(item));
		}
		model->endResetModel();
	}

	static bool hasStatus(MainWindow const &window, QString const &text)
	{
		for (auto *label : window.findChildren<StatusLabel *>()) {
			if (label->text() == text) return true;
		}
		return false;
	}

	static LocationData folderResult(FolderTreeItem const *item)
	{
		LocationData location;
		location.kind = static_cast<Kind>(item->data(0, KindRole).toInt());
		location.iidl = item->iidl();
		location.path = item->data(0, PathRole).toString();
		BasicFileSystemProvider provider(location.iidl);
		while (provider.fetch()) location.files.push_back(provider.fileInfo());
		MainWindow::sortFileInfoList(&location.files);
		return location;
	}

private slots:
	void partialCutRetainsOnlyUnmovedItems()
	{
		struct Restore {
			std::unique_ptr<QMimeData> data = std::make_unique<QMimeData>();
			Restore() { auto *mime = QApplication::clipboard()->mimeData(); if (mime) for (auto const &format : mime->formats()) data->setData(format, mime->data(format)); }
			~Restore() { QApplication::clipboard()->setMimeData(data.release()); }
		} restore;
		QTemporaryDir root(QDir::currentPath() + "/treewalker-partial-cut-XXXXXX"); QDir dir(root.path()); QVERIFY(dir.mkdir("source")); QVERIFY(dir.mkdir("target"));
		QVERIFY(createFile(dir.filePath("source/a"))); QVERIFY(createFile(dir.filePath("source/b"))); QVERIFY(createFile(dir.filePath("target/b")));
		MainWindow window; QVERIFY(prepareWindow(window)); window.makeTreeCompletely(); window.openDir(ItemIdList(dir.filePath("source")));
		QTRY_COMPARE(window.fileitemmodel()->rowCount(), 2);
		filetransfer::Request request{{dir.filePath("source/a"), dir.filePath("source/b")}, dir.filePath("target"), filetransfer::Operation::Move};
		QApplication::clipboard()->setMimeData(filetransfer::clipboardData(request.sources, request.operation, "partial-cut").release());
		QTimer timer; connect(&timer, &QTimer::timeout, [&] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) for (auto *button : box->buttons()) if (button->text() == "Skip") { button->click(); return; } }); timer.start(10);
		QSignalSpy done(&window, &MainWindow::transferFinished);
		window.startTransfer(request, "partial-cut");
		QTRY_COMPARE(done.count(), 1);
		QVERIFY(!QFileInfo::exists(dir.filePath("source/a"))); QVERIFY(QFileInfo::exists(dir.filePath("source/b"))); QVERIFY(QFileInfo::exists(dir.filePath("target/a")));
		auto remaining = filetransfer::clipboardRequest(QApplication::clipboard()->mimeData()); QCOMPARE(remaining.sources, QStringList{dir.filePath("source/b")}); QCOMPARE(remaining.operation, filetransfer::Operation::Move);
		QTRY_COMPARE(window.fileitemmodel()->rowCount(), 1);
	}

	void transferRefreshTracksLocation()
	{
		QTemporaryDir root(QDir::currentPath() + "/treewalker-transfer-refresh-XXXXXX"); QDir dir(root.path());
		QVERIFY(dir.mkpath("source/inner")); QVERIFY(dir.mkdir("target")); QVERIFY(dir.mkdir("elsewhere"));
		QVERIFY(createFile(dir.filePath("source/inner/data.txt"))); QVERIFY(createFile(dir.filePath("elsewhere/other.txt")));
		MainWindow window; QVERIFY(prepareWindow(window)); window.makeTreeCompletely(); window.openDir(ItemIdList(dir.filePath("elsewhere")));
		QTRY_COMPARE(window.fileitemmodel()->rowCount(), 1);
		filetransfer::Request request{{dir.filePath("source")}, dir.filePath("target"), filetransfer::Operation::Move};
		QVERIFY(QDir().rename(dir.filePath("source"), dir.filePath("target/source")));
		filetransfer::Results results{{dir.filePath("source"), dir.filePath("target/source"), filetransfer::Outcome::Completed, {}}};
		window.refreshAfterTransfer(request, results);
		QCOMPARE(QDir::fromNativeSeparators(window.currentIIDL().path()), QDir::fromNativeSeparators(dir.filePath("elsewhere")));
		window.openDir(ItemIdList(dir.filePath("target/source/inner")));
		QTRY_COMPARE(QDir::fromNativeSeparators(window.currentIIDL().path()), QDir::fromNativeSeparators(dir.filePath("target/source/inner")));
		QTRY_COMPARE(window.fileitemmodel()->rowCount(), 1);
		QVERIFY(QDir().rename(dir.filePath("target/source"), dir.filePath("source")));
		request.sources = {dir.filePath("target/source")}; request.destination = root.path();
		results = {{dir.filePath("target/source"), dir.filePath("source"), filetransfer::Outcome::Completed, {}}};
		window.refreshAfterTransfer(request, results);
		QTRY_COMPARE(QDir::fromNativeSeparators(window.currentIIDL().path()), QDir::fromNativeSeparators(dir.filePath("source/inner")));
		QTRY_COMPARE(window.fileitemmodel()->rowCount(), 1);
	}

	void transferShortcutsAcrossViews_data()
	{
		QTest::addColumn<int>("view"); QTest::addColumn<bool>("cut");
		for (int view = 0; view < 3; ++view) for (bool cut : {false, true}) QTest::newRow(qPrintable(QString("view-%1-%2").arg(view).arg(cut))) << view << cut;
	}
	void transferShortcutsAcrossViews()
	{
		QFETCH(int, view); QFETCH(bool, cut);
		struct ClipboardRestore {
			std::unique_ptr<QMimeData> saved = std::make_unique<QMimeData>();
			ClipboardRestore() { auto *old = QApplication::clipboard()->mimeData(); if (old) for (auto const &format : old->formats()) saved->setData(format, old->data(format)); }
			~ClipboardRestore() { QApplication::clipboard()->setMimeData(saved.release()); }
		} restore;
		QTemporaryDir root(QDir::currentPath() + "/treewalker-transfer-XXXXXX");
		QDir dir(root.path()); QVERIFY(dir.mkdir("source")); QVERIFY(dir.mkdir("target"));
		QVERIFY(createFile(dir.filePath("source/data.txt")));
		MainWindow window; QVERIFY(prepareWindow(window)); window.makeTreeCompletely();
		window.openDir(ItemIdList(dir.filePath("source")));
		QTRY_COMPARE(window.fileitemmodel()->rowCount(), 1);
		window.show(); window.activateWindow();
		QAbstractItemView *widget = nullptr;
		if (view == 0) { widget = window.findChild<FolderTreeView *>("treeView"); window.setFocusFolderTree(); }
		else {
			window.setViewMode(view == 1 ? MainWindow::List : MainWindow::Thumbnail);
			widget = view == 1 ? static_cast<QAbstractItemView *>(window.findChild<FileTableView *>("tableView")) : window.findChild<ThumbnailView *>("thumbnailView");
			window.setFocusItemsView();
			widget->selectionModel()->select(window.fileitemmodel()->index(0, 0), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
		}
		QVERIFY(widget); QTRY_VERIFY(widget->hasFocus());
		QTest::keyClick(widget, cut ? Qt::Key_X : Qt::Key_C, Qt::ControlModifier);
		auto request = filetransfer::clipboardRequest(QApplication::clipboard()->mimeData());
		QCOMPARE(request.sources.size(), 1);
		QCOMPARE(request.operation, cut ? filetransfer::Operation::Move : filetransfer::Operation::Copy);
		window.openDir(ItemIdList(dir.filePath("target")));
		QTRY_COMPARE(QDir::fromNativeSeparators(window.currentIIDL().path()), QDir::fromNativeSeparators(dir.filePath("target")));
		QTRY_COMPARE(window.fileitemmodel()->rowCount(), 0);
		widget->setFocus(); QTRY_VERIFY(widget->hasFocus());
		QSignalSpy finished(&window, &MainWindow::transferFinished);
		QTest::keyClick(widget, Qt::Key_V, Qt::ControlModifier);
		const bool clipboard_replaced = cut && view == 2;
		if (clipboard_replaced) QApplication::clipboard()->setText("new clipboard contents");
		QString copied = dir.filePath(view == 0 ? "target/source/data.txt" : "target/data.txt");
		QTRY_VERIFY(QFileInfo::exists(copied));
		QTRY_COMPARE(finished.count(), 1);
		QTRY_COMPARE(window.fileitemmodel()->rowCount(), 1);
		if (view != 0) QTRY_COMPARE(widget->currentIndex().data(PathRole).toString(), copied);
		QCOMPARE(QFileInfo::exists(dir.filePath("source/data.txt")), !cut);
		if (cut) QVERIFY(filetransfer::clipboardToken(QApplication::clipboard()->mimeData()).isEmpty());
		if (clipboard_replaced) QCOMPARE(QApplication::clipboard()->text(), QString("new clipboard contents"));
	}

	void providerDupKeepsDestination()
	{
		QTemporaryDir first, second;
		QVERIFY(first.isValid() && second.isValid());
		QVERIFY(createFile(second.filePath("only-in-second.txt")));
		BasicFileSystemProvider provider(ItemIdList(first.path()));
		auto duplicate = provider.dup(ItemIdList(second.path()));
		QCOMPARE(QDir::fromNativeSeparators(duplicate->currentDir()), QDir::fromNativeSeparators(second.path()));
		QVERIFY(duplicate->hasReadPermission());
		QVERIFY(duplicate->fetch());
		QCOMPARE(duplicate->fileInfo().path, second.filePath("only-in-second.txt"));
		QVERIFY(!duplicate->fetch());
	}

	void renameDialogReturnsEditedName()
	{
		RenameDialog dialog(RenameDialog::File, "/tmp/original.txt");
		auto *name = dialog.findChild<QLineEdit *>("lineEdit_basename");
		QVERIFY(name);
		name->setText("renamed");
		QCOMPARE(dialog.text(), QString("renamed.txt"));
	}

	void filesystemActionsValidateNamesAndPreserveExistingFiles()
	{
		QTemporaryDir files(QDir::currentPath() + "/treewalker-actions-XXXXXX");
		QVERIFY(files.isValid());
		QVERIFY(createFile(files.filePath("original.txt")));
		QVERIFY(createFile(files.filePath("existing.txt")));
		MainWindow window;
		QVERIFY(prepareWindow(window));
		window.makeTreeCompletely();
		window.openDir(ItemIdList(files.path()));
		QTRY_COMPARE(QDir::fromNativeSeparators(window.currentIIDL().path()), QDir::fromNativeSeparators(files.path()));
		QTRY_COMPARE(window.fileitemmodel()->rowCount(), 2);
		window.setViewMode(MainWindow::Thumbnail);
		auto *thumbnails = window.findChild<ThumbnailView *>("thumbnailView");
		QVERIFY(thumbnails);
		thumbnails->setCurrentIndex(window.fileitemmodel()->index(1, 0));
		QCOMPARE(window.currentFilePath(), thumbnails->currentIndex().data(PathRole).toString());
		QVERIFY(!window.renamePath(files.filePath("original.txt"), "existing.txt"));
		QVERIFY(!window.renamePath(files.filePath("original.txt"), "../outside.txt"));
		QVERIFY(!window.renamePath(files.filePath("original.txt"), ""));
		QVERIFY(QFileInfo::exists(files.filePath("original.txt")));
		QVERIFY(window.renamePath(files.filePath("original.txt"), "renamed.txt"));
		QVERIFY(!QFileInfo::exists(files.filePath("original.txt")));
		QVERIFY(QFileInfo::exists(files.filePath("renamed.txt")));
		QTRY_COMPARE(QDir::fromNativeSeparators(window.currentIIDL().path()), QDir::fromNativeSeparators(files.path()));
		QVERIFY(window.createFolder("created"));
		QVERIFY(QFileInfo(files.filePath("created")).isDir());
		QVERIFY(!window.createFolder("created"));
		QVERIFY(!window.createFolder("../outside"));
		QVERIFY(!window.createFolder(".."));
		QVERIFY(window.renamePath(files.filePath("created"), "renamed-folder"));
		QVERIFY(!QFileInfo::exists(files.filePath("created")));
		QVERIFY(QFileInfo(files.filePath("renamed-folder")).isDir());
		QTRY_COMPARE(QDir::fromNativeSeparators(window.currentIIDL().path()), QDir::fromNativeSeparators(files.filePath("renamed-folder")));
	}

	void treeFetchIsAsynchronousAndRejectsOldGeneration()
	{
		auto gate = std::make_shared<WorkerGate>();
		auto calls = std::make_shared<std::atomic<int>>(0);
		ReleaseWorkers cleanup{gate};
		MainWindow window(nullptr, {}, {}, [gate, calls](LocationData data, auto const &) {
			const int call = ++*calls;
			if (call == 1) gate->block();
			FileInfo2 child;
			child.isdir = true;
			child.name = call == 1 ? "old" : "new";
			child.path = "/" + child.name;
			child.iidl = ItemIdList(child.path);
			data.files.push_back(child);
			return data;
		});
		QVERIFY(prepareWindow(window));
		window.makeTreeCompletely();
		QTRY_VERIFY(gate->entered.available() >= 1);
		gate->entered.acquire();
		bool responsive = false;
		QTimer::singleShot(0, &window, [&] { responsive = true; });
		QTRY_VERIFY(responsive);
		auto *root = window.makeTreeCompletely();
		QTRY_COMPARE(root->childCount(), 1);
		QCOMPARE(root->child(0)->text(), QString("new"));
		gate->release.release();
		QVERIFY(gate->waitAll());
		QCoreApplication::processEvents();
		QCOMPARE(root->childCount(), 1);
		QCOMPARE(root->child(0)->text(), QString("new"));
	}

	void oldFolderResultDoesNotReplaceNewRequest_data()
	{
		QTest::addColumn<bool>("old_failure");
		QTest::newRow("old success") << false;
		QTest::newRow("old failure") << true;
	}

	void oldFolderResultDoesNotReplaceNewRequest()
	{
		QFETCH(bool, old_failure);
		QTemporaryDir files(QDir::currentPath() + "/treewalker-stale-request-XXXXXX");
		QVERIFY(files.isValid());
		const QString first = files.filePath("a");
		const QString second = files.filePath("b");
		QVERIFY(QDir().mkpath(first));
		QVERIFY(QDir().mkpath(second));
		auto gate = std::make_shared<WorkerGate>();
		auto calls = std::make_shared<std::atomic<int>>(0);
		MainWindow window(nullptr, [gate, calls, first, old_failure](LocationData data, auto const &) {
			bool old = data.path == first && ++*calls == 1;
			if (old) {
				gate->block();
				if (old_failure) throw QString("late stale error");
			}
			FileInfo2 info;
			info.name = old ? "old.txt" : "fresh.txt";
			info.path = data.path + "/" + info.name;
			data.files = {info};
			return data;
		});
		ReleaseWorkers cleanup{gate};
		QVERIFY(prepareWindow(window));
		QVERIFY(window.makeTreeCompletely());
		window.openDir(ItemIdList(first));
		QTRY_VERIFY(gate->entered.available() >= 1);
		gate->entered.acquire();
		window.openDir(ItemIdList(second));
		QTRY_COMPARE(window.fileitemmodel()->index(0, 0).data(PathRole).toString(), second + "/fresh.txt");
		window.openDir(ItemIdList(first));
		QTRY_COMPARE(window.fileitemmodel()->index(0, 0).data(PathRole).toString(), first + "/fresh.txt");
		gate->release.release();
		QVERIFY(gate->waitAll());
		QCoreApplication::processEvents();
		QCOMPARE(window.fileitemmodel()->index(0, 0).data(PathRole).toString(), first + "/fresh.txt");
		QVERIFY(!hasStatus(window, "late stale error"));
	}

	void shutdownSharesDeadline_data()
	{
		QTest::addColumn<int>("list_count");
		QTest::addColumn<int>("thumbnail_count");
		QTest::addColumn<bool>("late_failure");
		QTest::newRow("folder request") << 1 << 0 << false;
		QTest::newRow("thumbnail workers") << 0 << 8 << false;
		QTest::newRow("all workers") << 4 << 8 << false;
		QTest::newRow("late errors") << 4 << 8 << true;
	}

	void shutdownSharesDeadline()
	{
		QFETCH(int, list_count);
		QFETCH(int, thumbnail_count);
		QFETCH(bool, late_failure);
		QTemporaryDir files(QDir::currentPath() + "/treewalker-shutdown-XXXXXX");
		QVERIFY(files.isValid());
		for (int i = 0; i < list_count; ++i) QVERIFY(QDir().mkpath(files.filePath(QString::number(i))));
		auto gate = std::make_shared<WorkerGate>();
		auto window = std::make_unique<MainWindow>(nullptr,
			[gate, prefix = files.path(), list_count, late_failure](LocationData data, auto const &) {
				if (list_count && data.path.startsWith(prefix)) {
					gate->block();
					if (late_failure) throw QString("late folder error");
				}
				return data;
			}, [gate, late_failure](auto const &) {
				gate->block();
				if (late_failure) throw std::runtime_error("late thumbnail error");
				return QImage{};
			});
		ReleaseWorkers cleanup{gate};
		QVERIFY(prepareWindow(*window));
		window->reloadContents();
		for (int i = 0; i < list_count; ++i) {
			QString path = files.filePath(QString::number(i));
			window->openDir(ItemIdList(path));
			QTRY_VERIFY(gate->entered.available() >= i + 1);
		}
		for (int i = 0; i < thumbnail_count; ++i) {
			FileInfo2 info;
			info.path = files.filePath(QString("thumb-%1").arg(i));
			window->queryThumbnail(info);
		}
		QTRY_VERIFY(gate->entered.available() >= list_count + thumbnail_count);
		gate->entered.acquire(list_count + thumbnail_count);
		ReleaseWatchdog watchdog(gate);
		QElapsedTimer elapsed;
		elapsed.start();
		window.reset();
		global->mainwindow = nullptr;
		qInfo() << "Shutdown with" << list_count << "folder requests and" << thumbnail_count << "thumbnail workers:" << elapsed.elapsed() << "ms";
		QVERIFY(elapsed.elapsed() < 1800);
		gate->release.release(list_count + thumbnail_count);
		QVERIFY(gate->waitAll());
		QCoreApplication::processEvents();
	}

	void init()
	{
		global->incremental_search_text.clear();
		opened_urls_.clear();
		QDesktopServices::setUrlHandler("file", this, "recordUrl");
	}

	void cleanup()
	{
		QDesktopServices::unsetUrlHandler("file");
		global->incremental_search_text.clear();
		global->mainwindow = nullptr;
	}

	void recordUrl(QUrl const &url)
	{
		opened_urls_.push_back(url);
	}

	void treeClearResetsSearch_data()
	{
		QTest::addColumn<QString>("query");
		QTest::addColumn<int>("matches");
		QTest::newRow("zero matches") << QString("unmatched-query") << 0;
		QTest::newRow("one match") << QString("alpha") << 1;
		QTest::newRow("multiple matches") << QString("beta") << 2;
	}

	void treeClearResetsSearch()
	{
		QFETCH(QString, query);
		QFETCH(int, matches);
		MainWindow window;
		QVERIFY(prepareWindow(window));
		FolderTreeView tree;
		auto *root = new FolderTreeItem();
		root->setText(0, "alpha");
		for (QString const &name : {QString("beta-one"), QString("beta-two")}) {
			auto *child = new FolderTreeItem();
			child->setText(0, name);
			root->addChild(child);
		}
		tree.addTopLevelItem(root);
		tree.setCurrentItem(root);
		tree.setExpanded(root, true);
		tree.setFilter(query);
		QCOMPARE(tree.model()->rowCount(), matches);
		QPersistentModelIndex previous = tree.currentIndex();
		QSignalSpy selection_changes(&tree, &FolderTreeView::currentItemChanged);
		QVERIFY(selection_changes.isValid());

		tree.clear();

		QCOMPARE(selection_changes.count(), 0);
		QCOMPARE(tree.model()->rowCount(), 0);
		QVERIFY(tree.currentItem() == nullptr);
		QVERIFY(!previous.isValid());
		auto *replacement = new FolderTreeItem();
		replacement->setText(0, "replacement");
		tree.addTopLevelItem(replacement);
		QCOMPARE(tree.model()->rowCount(), 1);
		tree.setFilter({});
		QCOMPARE(tree.currentItem(), replacement);
		QCOMPARE(tree.model()->data(tree.model()->index(0, 0)).toString(), QString("replacement"));
	}

	void filteredActivationUsesSelectedFile_data()
	{
		QTest::addColumn<bool>("thumbnail");
		QTest::addColumn<bool>("enter");
		QTest::newRow("details double click") << false << false;
		QTest::newRow("thumbnail activation") << true << false;
		QTest::newRow("details enter") << false << true;
		QTest::newRow("thumbnail enter") << true << true;
	}

	void filteredActivationUsesSelectedFile()
	{
		QFETCH(bool, thumbnail);
		QFETCH(bool, enter);
		QTemporaryDir files;
		QVERIFY(files.isValid());
		QVERIFY(createFile(files.filePath("a.txt")));
		QVERIFY(createFile(files.filePath("b.txt")));
		MainWindow window;
		QVERIFY(prepareWindow(window));
		populateFiles(window, files.path());
		window.setViewMode(thumbnail ? MainWindow::Thumbnail : MainWindow::List);
		auto *table = window.findChild<FileTableView *>("tableView");
		auto *thumbnails = window.findChild<ThumbnailView *>("thumbnailView");
		auto *view = thumbnail ? static_cast<QAbstractItemView *>(thumbnails)
								: static_cast<QAbstractItemView *>(table);
		view->setCurrentIndex(window.fileitemmodel()->index(0, 0));
		QVERIFY(window.appendCharToFilterText("b", thumbnail
				? MainWindow::FilterTarget::ThumbnailViewSearch
				: MainWindow::FilterTarget::FileListViewSearch));
		QCOMPARE(window.fileitemmodel()->rowCount(), 1);
		QCOMPARE(window.fileitemmodel()->data(view->currentIndex(), PathRole).toString(), files.filePath("b.txt"));

		window.show(); window.activateWindow(); view->setFocus();
		QCoreApplication::processEvents(); QVERIFY(view->hasFocus());
		QTimer confirmation;
		connect(&confirmation, &QTimer::timeout, [&] {
			auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
			if (box && box->windowTitle() == "Open file") { confirmation.stop(); box->button(QMessageBox::Ok)->click(); }
		}); confirmation.start(10);
		if (enter) QTest::keyClick(view, Qt::Key_Return);
		else {
			QPoint position = view->visualRect(view->currentIndex()).center();
			QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, position);
			QTest::mouseDClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, position);
		}

		QCOMPARE(opened_urls_.size(), 1);
		QCOMPARE(opened_urls_.front().toLocalFile(), files.filePath("b.txt"));
		QCOMPARE(global->incremental_search_text, QString("b"));
	}

	void refreshDuringTreeSearch()
	{
		MainWindow window;
		QVERIFY(prepareWindow(window));
		auto *root = window.makeTreeCompletely();
		QVERIFY(root);
		auto *tree = window.findChild<FolderTreeView *>("treeView");
		QVERIFY(tree);
		{
			const QSignalBlocker blocker(tree);
			tree->setCurrentItem(root);
			window.setCurrentIIDL(root->iidl());
		}
		QVERIFY(window.appendCharToFilterText("u", MainWindow::FilterTarget::FolderTreeSearch));
		window.setIncrementalSearchText("unmatched-tree-query");
		QCOMPARE(tree->model()->rowCount(), 0);

		window.updateViews();

		QVERIFY(global->incremental_search_text.isEmpty());
		QVERIFY(tree->model()->rowCount() > 0);
		QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
		window.eventFilter(tree, &escape);
		QVERIFY(global->incremental_search_text.isEmpty());
	}

	void treeSearchDoesNotFilterFileList_data()
	{
		QTest::addColumn<bool>("thumbnail");
		QTest::newRow("details") << false;
		QTest::newRow("thumbnails") << true;
	}

	void refreshInvalidatesThumbnailCache()
	{
		QTemporaryDir files(QDir::currentPath() + "/treewalker-thumbnail-refresh-XXXXXX");
		QVERIFY(files.isValid());
		const QString path = files.filePath("image.bmp");
		QImage source(48, 24, QImage::Format_ARGB32);
		source.fill(Qt::red);
		QVERIFY(source.save(path));
		MainWindow window;
		QVERIFY(prepareWindow(window));
		window.reloadContents();
		window.openDir(ItemIdList(files.path()));
		QTRY_COMPARE(window.fileitemmodel()->rowCount(), 1);
		FileInfo2 info;
		BasicFileSystemProvider::setFileInfo(&info, "image.bmp", path);
		QTRY_VERIFY(!window.queryThumbnail(info).isNull());
		QCOMPARE(window.queryThumbnail(info).pixelColor(0, 0), QColor(Qt::red));
		source.fill(Qt::blue);
		QVERIFY(source.save(path));
		// Keep the same metadata in the request: only explicit F5 invalidation
		// should cause the already cached path to be read again.
		QCOMPARE(window.queryThumbnail(info).pixelColor(0, 0), QColor(Qt::red));
		QKeyEvent refresh(QEvent::KeyPress, Qt::Key_F5, Qt::NoModifier);
		window.keyPressEvent(&refresh);
		QTRY_VERIFY(!window.queryThumbnail(info).isNull());
		QCOMPARE(window.queryThumbnail(info).pixelColor(0, 0), QColor(Qt::blue));
	}

	void treeSearchDoesNotFilterFileList()
	{
		QFETCH(bool, thumbnail);
		QTemporaryDir files(QDir::currentPath() + "/treewalker-p108-tree-XXXXXX");
		QVERIFY(files.isValid());
		for (QString const &folder : {QString("p108searchneedle-one"), QString("p108searchneedle-two")}) {
			QVERIFY(QDir(files.path()).mkdir(folder));
			QVERIFY(createFile(files.filePath(folder + "/p108searchneedle.txt")));
			QVERIFY(createFile(files.filePath(folder + "/other.txt")));
		}
		MainWindow window;
		QVERIFY(prepareWindow(window));
		QVERIFY(window.makeTreeCompletely());
		auto *model = window.fileitemmodel();
		QSignalSpy initial_about_reset(model, &QAbstractItemModel::modelAboutToBeReset);
		QSignalSpy initial_reset(model, &QAbstractItemModel::modelReset);
		auto *tree = window.findChild<FolderTreeView *>("treeView");
		QVERIFY(tree);
		QAbstractItemModelTester tester(model, QAbstractItemModelTester::FailureReportingMode::QtTest);
		window.openDir(ItemIdList(files.filePath("p108searchneedle-one")));
		QTRY_COMPARE(model->rowCount(), 2);
		QCOMPARE(initial_about_reset.count(), 1);
		QCOMPARE(initial_reset.count(), 1);
		window.setViewMode(thumbnail ? MainWindow::Thumbnail : MainWindow::List);
		window.show();
		window.activateWindow();
		window.setFocusItemsView();
		QCoreApplication::processEvents();
		QAbstractItemView *view = thumbnail ? static_cast<QAbstractItemView *>(window.findChild<ThumbnailView *>("thumbnailView"))
			: window.findChild<FileTableView *>("tableView");
		QVERIFY(view);
		QTest::keyClicks(view, "other");
		QCOMPARE(model->rowCount(), 1);
		window.setFocusFolderTree();
		QCoreApplication::processEvents();
		QSignalSpy reset(model, &QAbstractItemModel::modelReset);
		QTest::keyClicks(tree, "p108searchneedle");
		tree->setCurrentItem(tree->itemFromIidl(ItemIdList(files.filePath("p108searchneedle-one"))));
		QTRY_VERIFY(model->rowCount() == 2 && model->index(0, 0).data(PathRole).toString() == files.filePath("p108searchneedle-one/other.txt"));
		QCOMPARE(global->incremental_search_text, QString("p108searchneedle"));
		QCOMPARE(model->rowCount(), 2);
		QCOMPARE(model->unfilteredCount(), 2);
		QVERIFY(!model->isFiltered());
		QVERIFY(hasStatus(window, "2 items"));

		for (QString const &folder : {QString("p108searchneedle-two"), QString("p108searchneedle-one")}) {
			reset.clear();
			auto *item = tree->itemFromIidl(ItemIdList(files.filePath(folder)));
			QVERIFY(item);
			tree->setCurrentItem(item);
			QTRY_VERIFY(reset.count() > 0);
			QCOMPARE(model->rowCount(), 2);
			QCOMPARE(model->unfilteredCount(), 2);
		}
		QTest::keyClick(tree, Qt::Key_Escape);
		QVERIFY(global->incremental_search_text.isEmpty());
		QCOMPARE(model->rowCount(), 2);
		QCOMPARE(model->unfilteredCount(), 2);

		QTest::keyClicks(tree, "unmatched-query");
		QVERIFY(!tree->currentItem());
		window.setFocusItemsView();
		QTest::keyClicks(view, "other");
		QCOMPARE(global->incremental_search_text, QString("other"));
		QTRY_COMPARE(model->rowCount(), 1);
		QCOMPARE(model->unfilteredCount(), 2);
		QCOMPARE(model->index(0, 0).data(PathRole).toString(), files.filePath("p108searchneedle-one/other.txt"));
		QTest::keyClicks(view, thumbnail ? "[" : "]");
		view = thumbnail ? static_cast<QAbstractItemView *>(window.findChild<FileTableView *>("tableView"))
			: window.findChild<ThumbnailView *>("thumbnailView");
		QTest::keyClicks(view, ".txt");
		QCOMPARE(global->incremental_search_text, QString("other.txt"));
		QCOMPARE(model->rowCount(), 1);
	}

	void fileSearchSurvivesRefresh_data()
	{
		QTest::addColumn<bool>("thumbnail");
		QTest::addColumn<QString>("query");
		for (bool thumbnail : {false, true}) {
			const QString prefix = thumbnail ? "thumbnails " : "details ";
			QTest::newRow(qPrintable(prefix + "matches")) << thumbnail << QString("beta");
			QTest::newRow(qPrintable(prefix + "zero matches")) << thumbnail << QString("unmatched-query");
			QTest::newRow(qPrintable(prefix + "no search")) << thumbnail << QString();
		}
	}

	void fileSearchSurvivesRefresh()
	{
		QFETCH(bool, thumbnail);
		QFETCH(QString, query);
		QTemporaryDir files(QDir::currentPath() + "/treewalker-p108-refresh-XXXXXX");
		QVERIFY(files.isValid());
		for (QString const &name : {QString("alpha.txt"), QString("beta1.txt"), QString("beta2.txt")}) {
			QVERIFY(createFile(files.filePath(name)));
		}
		MainWindow window;
		QVERIFY(prepareWindow(window));
		QVERIFY(window.makeTreeCompletely());
		window.openDir(ItemIdList(files.path()));
		auto *model = window.fileitemmodel();
		QAbstractItemModelTester tester(model, QAbstractItemModelTester::FailureReportingMode::QtTest);
		QTRY_COMPARE(model->rowCount(), 3);
		window.setViewMode(thumbnail ? MainWindow::Thumbnail : MainWindow::List);
		window.show();
		window.activateWindow();
		window.setFocusItemsView();
		QCoreApplication::processEvents();
		QAbstractItemView *view = thumbnail ? static_cast<QAbstractItemView *>(window.findChild<ThumbnailView *>("thumbnailView"))
			: window.findChild<FileTableView *>("tableView");
		QVERIFY(view);
		if (!query.isEmpty()) QTest::keyClicks(view, qPrintable(query));
		const bool zero_matches = query == "unmatched-query";
		if (!zero_matches) {
			view->selectionModel()->clearSelection();
			for (int row = 0; row < model->rowCount(); ++row) {
				auto index = model->index(row, 0);
				if (index.data(PathRole).toString().contains("beta")) {
					view->selectionModel()->select(index, QItemSelectionModel::Select | QItemSelectionModel::Rows);
					view->selectionModel()->setCurrentIndex(index, QItemSelectionModel::NoUpdate);
				}
			}
		}
		QVERIFY(createFile(files.filePath("beta0.txt")));
		int reset_depth = 0;
		connect(model, &QAbstractItemModel::modelAboutToBeReset, &window, [&] { QCOMPARE(++reset_depth, 1); });
		connect(model, &QAbstractItemModel::modelReset, &window, [&] { QCOMPARE(--reset_depth, 0); });
		QSignalSpy reset(model, &QAbstractItemModel::modelReset);

		QTest::keyClick(view, Qt::Key_F5);

		QCOMPARE(global->incremental_search_text, query);
		const int synchronous_resets = reset.count();
		QTRY_VERIFY(reset.count() > synchronous_resets);
		QTRY_COMPARE(model->unfilteredCount(), 4);
		QCOMPARE(model->rowCount(), zero_matches ? 0 : query.isEmpty() ? 4 : 3);
		QVERIFY(hasStatus(window, query.isEmpty() ? "4 items" : zero_matches ? "0/4 items" : "3/4 items"));
		if (!zero_matches) {
			QCOMPARE(view->currentIndex().data(PathRole).toString(), files.filePath("beta2.txt"));
			QStringList selected;
			for (auto const &index : view->selectionModel()->selectedRows()) selected.push_back(index.data(PathRole).toString());
			selected.sort();
			QCOMPARE(selected, (QStringList{files.filePath("beta1.txt"), files.filePath("beta2.txt")}));
		}
		QTest::keyClick(view, Qt::Key_Escape);
		QVERIFY(global->incremental_search_text.isEmpty());
		QCOMPARE(model->rowCount(), 4);
		QVERIFY(hasStatus(window, "4 items"));
	}

	void pendingRefreshUsesLatestSearch_data()
	{
		QTest::addColumn<bool>("thumbnail");
		QTest::addColumn<int>("action");
		for (bool thumbnail : {false, true}) {
			const QString prefix = thumbnail ? "thumbnails " : "details ";
			QTest::newRow(qPrintable(prefix + "change query")) << thumbnail << 0;
			QTest::newRow(qPrintable(prefix + "clear query")) << thumbnail << 1;
			QTest::newRow(qPrintable(prefix + "navigate away")) << thumbnail << 2;
		}
	}

	void pendingRefreshUsesLatestSearch()
	{
		QFETCH(bool, thumbnail);
		QFETCH(int, action);
		QTemporaryDir files(QDir::currentPath() + "/treewalker-p108-pending-XXXXXX");
		QVERIFY(files.isValid());
		QVERIFY(createFile(files.filePath("alpha.txt")));
		QVERIFY(createFile(files.filePath("beta.txt")));
		QVERIFY(QDir(files.path()).mkdir("other"));
		QVERIFY(createFile(files.filePath("other/other.txt")));
		MainWindow window;
		QVERIFY(prepareWindow(window));
		QVERIFY(window.makeTreeCompletely());
		window.openDir(ItemIdList(files.path()));
		auto *model = window.fileitemmodel();
		QAbstractItemModelTester tester(model, QAbstractItemModelTester::FailureReportingMode::QtTest);
		QTRY_COMPARE(model->rowCount(), 3);
		window.setViewMode(thumbnail ? MainWindow::Thumbnail : MainWindow::List);
		window.show();
		window.activateWindow();
		window.setFocusItemsView();
		QCoreApplication::processEvents();
		QAbstractItemView *view = thumbnail ? static_cast<QAbstractItemView *>(window.findChild<ThumbnailView *>("thumbnailView"))
			: window.findChild<FileTableView *>("tableView");
		QVERIFY(view);
		QTest::keyClicks(view, "alpha");
		QTest::keyClick(view, Qt::Key_F5);
		auto *tree = window.findChild<FolderTreeView *>("treeView");
		QVERIFY(tree);
		QTRY_VERIFY(tree->currentItem());
		const auto result = folderResult(tree->currentItem());
		// Deliver the saved directory result only after the intervening input.
		QMetaObject::invokeMethod(&window, [&window, result] { window.onRefreshFileListDone(result); }, Qt::QueuedConnection);
		QTest::keyClick(view, Qt::Key_Escape);
		if (action == 0) QTest::keyClicks(view, "beta");
		if (action == 2) window.openDir(ItemIdList(files.filePath("other")));
		QCoreApplication::sendPostedEvents(&window, QEvent::MetaCall);
		QCOMPARE(global->incremental_search_text, action == 0 ? QString("beta") : QString());
		if (action == 2) {
			QTRY_COMPARE(model->rowCount(), 1);
			QCOMPARE(model->index(0, 0).data(PathRole).toString(), files.filePath("other/other.txt"));
		} else {
			QCOMPARE(model->unfilteredCount(), 3);
			QCOMPARE(model->rowCount(), action == 0 ? 1 : 3);
			QVERIFY(hasStatus(window, action == 0 ? "1/3 items" : "3 items"));
		}
	}

#ifdef Q_OS_WIN
	void failedNavigationPreservesCurrentState_data()
	{
		QTest::addColumn<QByteArray>("pidl");
		QTest::newRow("empty pidl") << QByteArray();
		QTest::newRow("truncated terminator") << QByteArray(1, '\0');
		QTest::newRow("truncated component") << QByteArray::fromHex("100001020000");
		QTest::newRow("invalid component size") << QByteArray::fromHex("01000000");
		QTest::newRow("nonexistent path") << QByteArray("missing-path");
		QTest::newRow("empty address") << QByteArray("empty-address");
	}

	void failedNavigationPreservesCurrentState()
	{
		QFETCH(QByteArray, pidl);
		QTemporaryDir files;
		QVERIFY(files.isValid());
		QVERIFY(createFile(files.filePath("a.txt")));
		QVERIFY(createFile(files.filePath("b.txt")));
		MainWindow window;
		QVERIFY(prepareWindow(window));
		auto *root = window.makeTreeCompletely();
		QVERIFY(root);
		auto *tree = window.findChild<FolderTreeView *>("treeView");
		QVERIFY(tree);
		{
			const QSignalBlocker blocker(tree);
			tree->setCurrentItem(root);
			window.setCurrentIIDL(root->iidl());
		}
		populateFiles(window, files.path());
		QVERIFY(window.appendCharToFilterText("b", MainWindow::FilterTarget::FileListViewSearch));
		const ItemIdList current = window.currentIIDL();
		FolderTreeItem *selected = tree->currentItem();
		ItemIdList destination;
		if (pidl == "missing-path") {
			destination = ItemIdList(files.filePath("does-not-exist"));
		} else if (pidl == "empty-address") {
			destination = ItemIdList(QString());
		} else {
			destination = ItemIdList(pidl);
		}

		window.openDir(destination);

		QVERIFY(window.currentIIDL() == current);
		QCOMPARE(tree->currentItem(), selected);
		QCOMPARE(window.fileitemmodel()->rowCount(), 1);
		QCOMPARE(window.fileitemmodel()->data(window.fileitemmodel()->index(0, 0), PathRole).toString(), files.filePath("b.txt"));
		QCOMPARE(global->incremental_search_text, QString("b"));
	}

	void serializedPidlPreservesTrailingPayloadZeros()
	{
		const QByteArray bytes = QByteArray::fromHex("040001000000");
		const QString prefix = QString::fromLatin1(prefix_itemidlist.data(), prefix_itemidlist.size());
		const ItemIdList restored(prefix + QString::fromLatin1(bytes.toHex()));
		QVERIFY(restored.type() == ItemIdList::Type::WIN_SHELL_ITEMIDLIST);
		QCOMPARE(restored.iidl(), bytes);
	}

	void validDirectoryNavigation_data()
	{
		QTest::addColumn<bool>("drive_root");
		QTest::newRow("ordinary temporary directory") << false;
		QTest::newRow("drive root") << true;
	}

	void validDirectoryNavigation()
	{
		QFETCH(bool, drive_root);
		QTemporaryDir directory(QDir::currentPath() + "/treewalker-p1-navigation-XXXXXX");
		QVERIFY(directory.isValid());
		MainWindow window;
		QVERIFY(prepareWindow(window));
		QVERIFY(window.makeTreeCompletely());
		auto *tree = window.findChild<FolderTreeView *>("treeView");
		QVERIFY(tree);
		const ItemIdList destination(drive_root ? QDir::rootPath() : directory.path());
		QVERIFY(!destination.empty());

		window.openDir(destination);

		auto normalizedPath = [](QString const &path){
			return QDir::cleanPath(QDir::fromNativeSeparators(path));
		};
		const QString expected_path = normalizedPath(destination.path());
		QTRY_COMPARE(normalizedPath(window.currentIIDL().path()), expected_path);
		QVERIFY(tree->currentItem());
		QCOMPARE(normalizedPath(tree->currentItem()->iidl().path()), expected_path);
	}

	void desktopTerminatorIsValidNavigation()
	{
		MainWindow window;
		QVERIFY(prepareWindow(window));
		QVERIFY(window.makeTreeCompletely());
		auto *tree = window.findChild<FolderTreeView *>("treeView");
		QVERIFY(tree);
		const ItemIdList desktop(QByteArray(2, '\0'));
		FolderTreeItem *item = tree->itemFromIidl(desktop);
		QVERIFY(item);
		QCOMPARE(window.openDirWindows(desktop), item);
	}
#endif
};

int main(int argc, char **argv)
{
	QApplication app(argc, argv);
	if (app.arguments().contains("--blocked-workers-child")) return blockedWorkersChild();
	if (app.arguments().contains("--plugin-error-child")) return pluginErrorChild(app.arguments().value(app.arguments().indexOf("--plugin-error-child") + 1));
#ifdef Q_OS_WIN
	WindowsComApartment apartment;
	if (!apartment.initialized()) return 1;
#endif
	ApplicationGlobal application_global;
	global = &application_global;
	global->organization_name = ORGANIZATION_NAME;
	global->application_name = APPLICATION_NAME;
	QTemporaryDir config;
	if (!config.isValid()) return 1;
	global->app_config_dir = config.path();
	global->config_file_path = config.filePath("TreeWalker.ini");
	global->incremental_search = loadPlugin<IncrementalSearch, IncrementalSearchInterface>("incrementalsearchplugin");
	if (!global->incremental_search) return 1;
	qRegisterMetaType<LocationData>("LocationData");
	qRegisterMetaType<ItemIdList>("ItemIdList");
	qRegisterMetaType<FolderTreeItem *>("FolderTreeItem*");
	P1RegressionTest test;
	int result = QTest::qExec(&test, argc, argv);
	result |= runFileOpenTests(argc, argv);
	result |= runModelNotificationTests(argc, argv);
	result |= runAddressInputTests(argc, argv);
	result |= runSpecialLocationTests(argc, argv);
	result |= runStartupSettingsTests(argc, argv);
	result |= runReadingSearchTests(argc, argv);
	result |= runThumbnailLoaderTests(argc, argv);
	result |= runWorkerShutdownTests(argc, argv);
	result |= runPluginLoadingTests(argc, argv);
#ifdef Q_OS_WIN
	result |= runNetworkDiscoveryTests(argc, argv);
	result |= runComWorkerTests(argc, argv);
#endif
	return result;
}

#include "p1_regression.moc"
