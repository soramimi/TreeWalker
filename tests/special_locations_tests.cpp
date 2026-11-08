#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QScopedValueRollback>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include "ApplicationGlobal.h"
#include "FileTableView.h"
#include "FolderTreeView.h"
#include "MainWindow.h"
#include "ThumbnailView.h"
#ifdef Q_OS_WIN
#include "WindowsFileSystemProvider.h"
#endif

class SpecialLocationTests : public QObject {
	Q_OBJECT
private:
	QList<QUrl> opened_urls_;

	static QJsonObject url(QString const &id, QString const &name, QString const &address)
	{
		return {{"id", id}, {"type", "url"}, {"name", name}, {"url", address}};
	}

	static QJsonObject folder(QString const &id, QString const &name, QJsonArray const &children = {})
	{
		return {{"id", id}, {"type", "folder"}, {"name", name}, {"children", children}};
	}

	static bool writeBookmarks(QString const &path, bool changed = false)
	{
		QJsonArray children{
			folder("10", "same / name", {url("100", "First", "https://first.example/test?q=1")}),
			folder("11", "same / name", {url("101", "Second", "http://second.example/page")}),
			folder("12", "Empty / folder"),
			folder("13", "Parent", {
				folder("14", "Nested", {url("102", changed ? "Updated" : "Original", changed
					? "https://updated.example/" : "https://original.example/")})
			}),
			url("15", "Top URL", "https://top.example/")
		};
		if (changed) children.removeAt(1);
		QJsonObject roots{
			{"bookmark_bar", folder("1", "Test bookmarks", children)},
			{"other", folder("2", "Other")}
		};
		QFile file(path);
		if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
		QByteArray data = QJsonDocument(QJsonObject{{"roots", roots}}).toJson();
		return file.write(data) == data.size();
	}

	bool prepareWindow(MainWindow &window, QString const &source)
	{
		auto *table = window.findChild<FileTableView *>("tableView");
		auto *thumbnails = window.findChild<ThumbnailView *>("thumbnailView");
		if (!table || !thumbnails) return false;
		table->setModel(window.fileitemmodel());
		thumbnails->setModel(window.fileitemmodel());
		window.fetchBookmarks(source);
		return window.makeTreeCompletely() != nullptr;
	}

	static FolderTreeItem *findBookmarkRoot(FolderTreeItem *item)
	{
		if (!item) return nullptr;
		if (static_cast<Kind>(item->data(0, KindRole).toInt()) == Kind::ChromeBookmark) return item;
		for (int i = 0; i < item->childCount(); ++i) {
			if (auto *found = findBookmarkRoot(item->child(i))) return found;
		}
		return nullptr;
	}

	static FolderTreeItem *bookmarkRoot(FolderTreeView *tree)
	{
		if (!tree) return nullptr;
		for (int row = 0; row < tree->model()->rowCount(); ++row) {
			if (auto *found = findBookmarkRoot(tree->itemFromIndex(tree->model()->index(row, 0)))) return found;
		}
		return nullptr;
	}

	static FolderTreeItem *childNamed(FolderTreeItem *parent, QString const &name, int occurrence = 0)
	{
		if (!parent) return nullptr;
		for (int i = 0; i < parent->childCount(); ++i) {
			auto *child = parent->child(i);
			if (child->text() == name && occurrence-- == 0) return child;
		}
		return nullptr;
	}

	static QModelIndex rowNamed(FileItemModel *model, QString const &name, int occurrence = 0)
	{
		for (int row = 0; row < model->rowCount(); ++row) {
			auto index = model->index(row, 0);
			if (index.data().toString() == name && occurrence-- == 0) return index;
		}
		return {};
	}

	static bool createFile(QString const &path)
	{
		QFile file(path);
		return file.open(QIODevice::WriteOnly) && file.write("test\n") == 5;
	}

private slots:
	void init()
	{
		global->incremental_search_text.clear();
		opened_urls_.clear();
		QDesktopServices::setUrlHandler("http", this, "recordUrl");
		QDesktopServices::setUrlHandler("https", this, "recordUrl");
	}

	void cleanup()
	{
		QDesktopServices::unsetUrlHandler("http");
		QDesktopServices::unsetUrlHandler("https");
		global->incremental_search_text.clear();
	}

	void recordUrl(QUrl const &url)
	{
		opened_urls_.push_back(url);
	}

	void bookmarkSelectionReplacesDirectoryAndClearsEmptyFolder()
	{
		QTemporaryDir source;
		QTemporaryDir directory(QDir::currentPath() + "/treewalker-special-locations-XXXXXX");
		QVERIFY(source.isValid());
		QVERIFY(directory.isValid());
		QString const bookmark_file = source.filePath("Bookmarks");
		QVERIFY(writeBookmarks(bookmark_file));
		QVERIFY(createFile(directory.filePath("ordinary.txt")));
		MainWindow window;
		QScopedValueRollback<MainWindow *> main_window(global->mainwindow, &window);
		QVERIFY(prepareWindow(window, bookmark_file));
		auto *tree = window.findChild<FolderTreeView *>("treeView");
		auto *root = bookmarkRoot(tree);
		auto *bar = childNamed(root, "Test bookmarks");
		auto *empty = childNamed(bar, "Empty / folder");
		QVERIFY(root);
		QVERIFY(bar);
		QVERIFY(empty);
		auto *model = window.fileitemmodel();
		window.openDir(ItemIdList(directory.path()));
		QTRY_VERIFY_WITH_TIMEOUT(rowNamed(model, "ordinary.txt").isValid(), 5000);
		LocationData stale;
		stale.kind = static_cast<Kind>(tree->currentItem()->data(0, KindRole).toInt());
		stale.iidl = tree->currentItem()->data(0, IidlRole).value<ItemIdList>();
		stale.path = tree->currentItem()->data(0, PathRole).toString();
		FileInfo2 stale_file;
		stale_file.name = "stale.txt";
		stale_file.path = directory.filePath("stale.txt");
		stale.files.push_back(stale_file);

		tree->setCurrentItem(bar);

		QCOMPARE(model->kind_, Kind::ChromeBookmark);
		QCOMPARE(model->columnCount(), 2);
		QCOMPARE(model->rowCount(), 5);
		QVERIFY(rowNamed(model, "Top URL").isValid());
		QVERIFY(!rowNamed(model, "ordinary.txt").isValid());
		QVERIFY(window.locationInfo(bar));
		QVERIFY(window.locationInfo(window.location(bar)));

		// Interrupt an in-flight filesystem refresh by selecting a bookmark.
		window.openDir(ItemIdList(directory.path()));
		tree->setCurrentItem(empty);
		QCOMPARE(model->kind_, Kind::ChromeBookmark);
		QCOMPARE(model->rowCount(), 0);
		window.onRefreshFileListDone(stale);
		QCOMPARE(model->kind_, Kind::ChromeBookmark);
		QCOMPARE(model->rowCount(), 0);
		QTest::qWait(400);
		QCOMPARE(model->kind_, Kind::ChromeBookmark);
		QCOMPARE(model->rowCount(), 0);

		tree->setCurrentItem(root);
		QCOMPARE(model->rowCount(), 2);
		QVERIFY(rowNamed(model, "Test bookmarks").isValid());
		QVERIFY(rowNamed(model, "Other").isValid());
		QCOMPARE(window.findChild<FileTableView *>("tableView")->kind(), Kind::ChromeBookmark);
		QCOMPARE(window.findChild<ThumbnailView *>("thumbnailView")->model(), model);
	}

	void duplicateFolderNamesActivateTheirOwnUrls_data()
	{
		QTest::addColumn<int>("occurrence");
		QTest::addColumn<QString>("address");
		QTest::newRow("first duplicate https") << 0 << QString("https://first.example/test?q=1");
		QTest::newRow("second duplicate http") << 1 << QString("http://second.example/page");
	}

	void duplicateFolderNamesActivateTheirOwnUrls()
	{
		QFETCH(int, occurrence);
		QFETCH(QString, address);
		QTemporaryDir source;
		QVERIFY(source.isValid());
		QString const bookmark_file = source.filePath("Bookmarks");
		QVERIFY(writeBookmarks(bookmark_file));
		MainWindow window;
		QScopedValueRollback<MainWindow *> main_window(global->mainwindow, &window);
		QVERIFY(prepareWindow(window, bookmark_file));
		auto *tree = window.findChild<FolderTreeView *>("treeView");
		auto *bar = childNamed(bookmarkRoot(tree), "Test bookmarks");
		auto *first = childNamed(bar, "same / name", 0);
		auto *second = childNamed(bar, "same / name", 1);
		QVERIFY(first);
		QVERIFY(second);
		QVERIFY(window.location(first) != window.location(second));
		tree->setCurrentItem(bar);
		auto *model = window.fileitemmodel();
		auto index = rowNamed(model, "same / name", occurrence);
		QVERIFY(index.isValid());

		window.openTableItem(index);

		QCOMPARE(tree->currentItem(), occurrence == 0 ? first : second);
		QCOMPARE(model->kind_, Kind::ChromeBookmark);
		QCOMPARE(model->rowCount(), 1);
		QCOMPARE(model->index(0, 0).data(UrlRole).toString(), address);
		QVERIFY(opened_urls_.isEmpty());
		window.openTableItem(model->index(0, 0));
		QCOMPARE(opened_urls_.size(), 1);
		QCOMPARE(opened_urls_.front(), QUrl(address));
	}

	void refreshReloadsBookmarksAndPreservesNestedSelection()
	{
		QTemporaryDir source;
		QVERIFY(source.isValid());
		QString const bookmark_file = source.filePath("Bookmarks");
		QVERIFY(writeBookmarks(bookmark_file));
		MainWindow window;
		QScopedValueRollback<MainWindow *> main_window(global->mainwindow, &window);
		QVERIFY(prepareWindow(window, bookmark_file));
		auto *tree = window.findChild<FolderTreeView *>("treeView");
		auto *bar = childNamed(bookmarkRoot(tree), "Test bookmarks");
		auto *nested = childNamed(childNamed(bar, "Parent"), "Nested");
		QVERIFY(nested);
		tree->setCurrentItem(nested);
		QString const selected_location = window.currentLocation();
		auto *model = window.fileitemmodel();
		QCOMPARE(model->rowCount(), 1);
		QCOMPARE(model->index(0, 0).data().toString(), QString("Original"));
		QVERIFY(writeBookmarks(bookmark_file, true));
		QSignalSpy resets(model, &QAbstractItemModel::modelReset);
		QVERIFY(resets.isValid());

		QKeyEvent refresh(QEvent::KeyPress, Qt::Key_F5, Qt::NoModifier);
		window.keyPressEvent(&refresh);

		QVERIFY(resets.count() > 0);
		QCOMPARE(window.currentLocation(), selected_location);
		QVERIFY(tree->currentItem());
		QCOMPARE(tree->currentItem()->text(), QString("Nested"));
		QVERIFY(tree->currentItem()->parent());
		QCOMPARE(tree->currentItem()->parent()->text(), QString("Parent"));
		QCOMPARE(model->kind_, Kind::ChromeBookmark);
		QCOMPARE(model->rowCount(), 1);
		QCOMPARE(model->index(0, 0).data().toString(), QString("Updated"));
		QCOMPARE(model->index(0, 0).data(UrlRole).toString(), QString("https://updated.example/"));
		bar = childNamed(bookmarkRoot(tree), "Test bookmarks");
		QVERIFY(bar);
		QCOMPARE(bar->childCount(), 3);
		QVERIFY(childNamed(bar, "same / name", 0));
		QVERIFY(!childNamed(bar, "same / name", 1));
		tree->setCurrentItem(bar);
		QCOMPARE(model->rowCount(), 4);
	}

	void myComputerBookmarkRowOpensBookmarkRoot()
	{
		QTemporaryDir source;
		QVERIFY(source.isValid());
		QString const bookmark_file = source.filePath("Bookmarks");
		QVERIFY(writeBookmarks(bookmark_file));
		MainWindow window;
		QScopedValueRollback<MainWindow *> main_window(global->mainwindow, &window);
		QVERIFY(prepareWindow(window, bookmark_file));
		auto *tree = window.findChild<FolderTreeView *>("treeView");
		auto *root = bookmarkRoot(tree);
		QVERIFY(root);
		QString const computer = QString::fromLatin1(prefix_mycomputer.data(), prefix_mycomputer.size());
		window.openDir(ItemIdList(computer));
		QCOMPARE(window.currentLocation(), computer);
		QVERIFY(window.locationInfo(computer));
		auto *computer_item = tree->currentItem();
		QVERIFY(computer_item);
		int const child_count = computer_item->childCount();
		QVERIFY(child_count > 0);

		// Right-arrow refresh must keep this synthetic container's own tree.
		window.updateCurrentFolder();

		QCOMPARE(tree->currentItem(), computer_item);
		QCOMPARE(computer_item->childCount(), child_count);
		QCOMPARE(bookmarkRoot(tree), root);
		auto *model = window.fileitemmodel();
		QCOMPARE(model->kind_, Kind::Directory);
		auto index = rowNamed(model, root->text());
		QVERIFY(index.isValid());
		QCOMPARE(index.data(KindRole).toInt(), static_cast<int>(Kind::ChromeBookmark));
		QCOMPARE(index.data(IidlRole).value<ItemIdList>().path(), window.location(root));

		window.openTableItem(index);

		QCOMPARE(tree->currentItem(), root);
		QCOMPARE(model->kind_, Kind::ChromeBookmark);
		QCOMPARE(model->rowCount(), 2);
	}

	void hiddenFilesTogglePreservesBookmarkHierarchy()
	{
		QTemporaryDir source;
		QVERIFY(source.isValid());
		QString const bookmark_file = source.filePath("Bookmarks");
		QVERIFY(writeBookmarks(bookmark_file));
		MainWindow window;
		QScopedValueRollback<MainWindow *> main_window(global->mainwindow, &window);
		QVERIFY(prepareWindow(window, bookmark_file));
		auto *tree = window.findChild<FolderTreeView *>("treeView");
		auto *bar = childNamed(bookmarkRoot(tree), "Test bookmarks");
		auto *parent = childNamed(bar, "Parent");
		auto *nested = childNamed(parent, "Nested");
		QVERIFY(bar);
		QVERIFY(parent);
		QVERIFY(nested);
		tree->setCurrentItem(bar);

		for (bool visible : {true, false}) {
			window.setHiddenFilesVisible(visible);
			QCOMPARE(tree->currentItem(), bar);
			QCOMPARE(bar->childCount(), 4);
			QCOMPARE(childNamed(bar, "Parent"), parent);
			QCOMPARE(childNamed(parent, "Nested"), nested);
			QCOMPARE(window.fileitemmodel()->kind_, Kind::ChromeBookmark);
			QCOMPARE(window.fileitemmodel()->rowCount(), 5);
		}
	}

#ifdef Q_OS_WIN
	void pathlessShellFolderRowUsesItsItemIdList()
	{
		QTemporaryDir source;
		QVERIFY(source.isValid());
		QString const bookmark_file = source.filePath("Bookmarks");
		QVERIFY(writeBookmarks(bookmark_file));
		MainWindow window;
		QScopedValueRollback<MainWindow *> main_window(global->mainwindow, &window);
		QVERIFY(prepareWindow(window, bookmark_file));
		auto *tree = window.findChild<FolderTreeView *>("treeView");
		QVERIFY(tree);
		ItemIdList const drives = FileItem::getDrives().idlist();
		QVERIFY(!drives.empty());
		QVERIFY(drives.path().isEmpty());
		auto *model = window.fileitemmodel();
		model->beginResetModel();
		model->clearItems();
		model->setKind(Kind::Directory);
		FileItemModel::Item item;
		item.name = "Virtual drives folder";
		item.info.name = item.name;
		item.info.isdir = true;
		item.info.iidl = drives;
		model->addItem(std::move(item));
		model->endResetModel();
		auto index = model->index(0, 0);
		QVERIFY(index.data(PathRole).toString().isEmpty());
		QVERIFY(index.data(UrlRole).toString().isEmpty());
		QCOMPARE(index.data(IidlRole).value<ItemIdList>(), drives);
		QSignalSpy resets(model, &QAbstractItemModel::modelReset);
		QVERIFY(resets.isValid());

		window.openTableItem(index);

		QVERIFY(tree->currentItem());
		QCOMPARE(tree->currentItem()->iidl(), drives);
		QString const prefix = QString::fromLatin1(prefix_itemidlist.data(), prefix_itemidlist.size());
		QVERIFY(window.currentLocation().startsWith(prefix));
		QVERIFY(window.locationInfo(window.currentLocation()));
		QTRY_VERIFY_WITH_TIMEOUT(resets.count() > 0, 5000);
		QCOMPARE(model->columnCount(), 4);
		QVERIFY(model->kind_ == Kind::Directory || model->kind_ == Kind::SubDirectory);
		QString const drive_path = QDir::cleanPath(QDir::fromNativeSeparators(QDir::rootPath()));
		bool found_drive = false;
		for (int row = 0; row < model->rowCount(); ++row) {
			QString const path = model->index(row, 0).data(PathRole).toString();
			if (QDir::cleanPath(QDir::fromNativeSeparators(path)) == drive_path) found_drive = true;
		}
		QVERIFY(found_drive);
	}

	void shellAndBookmarkLocationsCanAlternate()
	{
		QTemporaryDir source;
		QVERIFY(source.isValid());
		QString const bookmark_file = source.filePath("Bookmarks");
		QVERIFY(writeBookmarks(bookmark_file));
		MainWindow window;
		QScopedValueRollback<MainWindow *> main_window(global->mainwindow, &window);
		QVERIFY(prepareWindow(window, bookmark_file));
		auto *tree = window.findChild<FolderTreeView *>("treeView");
		auto *root = bookmarkRoot(tree);
		auto *bar = childNamed(root, "Test bookmarks");
		QVERIFY(bar);
		auto *model = window.fileitemmodel();
		ItemIdList const desktop(QByteArray(2, '\0'));

		window.openDir(desktop);
		QTRY_VERIFY_WITH_TIMEOUT(model->kind_ == static_cast<Kind>(tree->currentItem()->data(0, KindRole).toInt()), 5000);
		QCOMPARE(model->columnCount(), 4);
		QVERIFY(window.locationInfo(tree->currentItem()));
		QVERIFY(window.locationInfo(window.currentLocation()));
		tree->setCurrentItem(bar);
		QCOMPARE(model->kind_, Kind::ChromeBookmark);
		QCOMPARE(model->rowCount(), 5);
		window.openDir(desktop);
		QTRY_VERIFY_WITH_TIMEOUT(model->kind_ != Kind::ChromeBookmark, 5000);
		QCOMPARE(tree->currentItem()->iidl(), desktop);
		tree->setCurrentItem(root);
		QCOMPARE(model->kind_, Kind::ChromeBookmark);
		QCOMPARE(model->rowCount(), 2);
	}
#endif
};

int runSpecialLocationTests(int argc, char **argv)
{
	SpecialLocationTests test;
	return QTest::qExec(&test, argc, argv);
}

#include "special_locations_tests.moc"
