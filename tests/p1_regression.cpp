#include <QApplication>
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
#include "FileTableView.h"
#include "FolderTreeView.h"
#include "LoadPlugin.h"
#include "MainWindow.h"
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
#ifdef Q_OS_WIN
	result |= runNetworkDiscoveryTests(argc, argv);
	result |= runComWorkerTests(argc, argv);
#endif
	return result;
}

#include "p1_regression.moc"
