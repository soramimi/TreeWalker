#include <QApplication>
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

#include "ApplicationGlobal.h"
#include "BasicFileSystemProvider.h"
#include "FileTableView.h"
#include "FolderTreeView.h"
#include "LoadPlugin.h"
#include "MainWindow.h"
#include "StatusLabel.h"
#include "ThumbnailView.h"
#include "subprojects/IncrementalSearchPlugin/src/IncrementalSearchInterface.h"
#ifdef Q_OS_WIN
#include "WindowsCom.h"
#include "WindowsShellAPI.h"
#endif

ApplicationGlobal *global = nullptr;

int runModelNotificationTests(int argc, char **argv);
int runAddressInputTests(int argc, char **argv);
int runSpecialLocationTests(int argc, char **argv);
int runStartupSettingsTests(int argc, char **argv);
int runReadingSearchTests(int argc, char **argv);

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

		if (enter) {
			window.show();
			window.activateWindow();
			view->setFocus();
			QCoreApplication::processEvents();
			QVERIFY(view->hasFocus());
			QKeyEvent event(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
			window.keyPressEvent(&event);
		} else {
			window.openTableItem(view->currentIndex());
		}

		QCOMPARE(opened_urls_.size(), 1);
		QCOMPARE(opened_urls_.front().toLocalFile(), files.filePath("b.txt"));
		QVERIFY(global->incremental_search_text.isEmpty());
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
		window.openDir(ItemIdList(files.filePath("p108searchneedle-one")));
		auto *model = window.fileitemmodel();
		auto *tree = window.findChild<FolderTreeView *>("treeView");
		QVERIFY(tree);
		QAbstractItemModelTester tester(model, QAbstractItemModelTester::FailureReportingMode::QtTest);
		QTRY_COMPARE(model->rowCount(), 2);
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
		QCOMPARE(model->unfilteredCount(), 4);
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
		QVERIFY(tree && tree->currentItem());
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
		QCOMPARE(normalizedPath(window.currentIIDL().path()), expected_path);
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
	result |= runModelNotificationTests(argc, argv);
	result |= runAddressInputTests(argc, argv);
	result |= runSpecialLocationTests(argc, argv);
	result |= runStartupSettingsTests(argc, argv);
	result |= runReadingSearchTests(argc, argv);
#ifdef Q_OS_WIN
	result |= runNetworkDiscoveryTests(argc, argv);
	result |= runComWorkerTests(argc, argv);
#endif
	return result;
}

#include "p1_regression.moc"
