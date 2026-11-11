#include "ApplicationGlobal.h"
#include "BasicFileSystemProvider.h"
#include "FileTableView.h"
#include "FolderTreeView.h"
#include "IncrementalSearchHelper.h"
#include "MainWindow.h"
#include "ThumbnailView.h"

#include <QApplication>
#include <QAbstractItemModelTester>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMimeData>
#include <QTemporaryDir>
#include <QTest>
#include <memory>

namespace {

class ClipboardRestorer {
	QClipboard *clipboard_ = QApplication::clipboard();
	std::unique_ptr<QMimeData> previous_ = std::make_unique<QMimeData>();
public:
	ClipboardRestorer()
	{
		if (auto *data = clipboard_->mimeData()) {
			for (auto const &format : data->formats()) {
				previous_->setData(format, data->data(format));
			}
		}
	}
	~ClipboardRestorer()
	{
		clipboard_->setMimeData(previous_.release());
	}
};

} // namespace

class AddressInputTests : public QObject {
	Q_OBJECT
private:
	std::unique_ptr<MainWindow> window_;
	MainWindow *previous_window_ = nullptr;
	QLineEdit *address_ = nullptr;
	FolderTreeView *tree_ = nullptr;
	FileTableView *table_ = nullptr;
	ThumbnailView *thumbnails_ = nullptr;

	FolderTreeItem *addDirectoryRoot(QString const &path)
	{
		auto *root = new FolderTreeItem;
		FileInfo2 info;
		BasicFileSystemProvider::setFileInfo(&info, "test root", path);
		root->setText(0, info.name);
		root->setData(0, KindRole, int(Kind::Directory));
		root->setData(0, PathRole, path);
		root->setData(0, IidlRole, QVariant::fromValue(info.iidl));
		auto *placeholder = new FolderTreeItem;
		placeholder->setData(0, KindRole, int(Kind::Placeholder));
		root->addChild(placeholder);
		tree_->addTopLevelItem(root);
		tree_->setExpanded(root, true);
		return root;
	}

private slots:
	void init()
	{
		previous_window_ = global->mainwindow;
		global->incremental_search_text.clear();
		window_ = std::make_unique<MainWindow>();
		global->mainwindow = window_.get();
		address_ = window_->findChild<QLineEdit *>("lineEdit_address");
		tree_ = window_->findChild<FolderTreeView *>("treeView");
		table_ = window_->findChild<FileTableView *>("tableView");
		thumbnails_ = window_->findChild<ThumbnailView *>("thumbnailView");
		QVERIFY(address_ && tree_ && table_ && thumbnails_);
		table_->setModel(window_->fileitemmodel());
		thumbnails_->setModel(window_->fileitemmodel());
		table_->setKind(Kind::Directory);
		thumbnails_->setKind(Kind::Directory);
		window_->setViewMode(MainWindow::List);
		window_->show();
		window_->activateWindow();
		QCoreApplication::processEvents();
	}

	void cleanup()
	{
		window_.reset();
		global->mainwindow = previous_window_;
		global->incremental_search_text.clear();
	}

	void addressTextEditing_data()
	{
		QTest::addColumn<bool>("thumbnail");
		QTest::newRow("detailed view") << false;
		QTest::newRow("thumbnail view") << true;
	}

	void addressTextEditing()
	{
		QFETCH(bool, thumbnail);
		auto mode = thumbnail ? MainWindow::Thumbnail : MainWindow::List;
		window_->setViewMode(mode);
		window_->setAddressBarVisible(true);
		QVERIFY(address_->hasFocus());
		address_->clear();
		QTest::keyClicks(address_, "C:/Docs[1]/draft[D]");
		QCOMPARE(address_->text(), QString("C:/Docs[1]/draft[D]"));
		QCOMPARE(window_->viewmode(), mode);

		QTest::keyClick(address_, Qt::Key_Home);
		QTest::keyClick(address_, Qt::Key_Right, Qt::ShiftModifier);
		QTest::keyClicks(address_, "D");
		QTest::keyClick(address_, Qt::Key_End);
		QTest::keyClick(address_, Qt::Key_Backspace);
		QTest::keyClicks(address_, "d]");
		QCOMPARE(address_->text(), QString("D:/Docs[1]/draft[Dd]"));
		QCOMPARE(window_->viewmode(), mode);

		ClipboardRestorer clipboard;
		QApplication::clipboard()->setText("C:/[draft]/Docs[D]");
		QTest::keyClick(address_, Qt::Key_A, Qt::ControlModifier);
		QTest::keyClick(address_, Qt::Key_V, Qt::ControlModifier);
		QCOMPARE(address_->text(), QString("C:/[draft]/Docs[D]"));
		QCOMPARE(window_->viewmode(), mode);
		QVERIFY(global->incremental_search_text.isEmpty());
	}

	void addressShortcuts_data()
	{
		QTest::addColumn<int>("source");
		QTest::addColumn<int>("key");
		QTest::addColumn<Qt::KeyboardModifiers>("modifiers");
		for (int source = 0; source < 3; ++source) {
			QTest::newRow(qPrintable(QString("Alt+D from %1").arg(source)))
					<< source << int(Qt::Key_D) << Qt::KeyboardModifiers(Qt::AltModifier);
			QTest::newRow(qPrintable(QString("Ctrl+L from %1").arg(source)))
					<< source << int(Qt::Key_L) << Qt::KeyboardModifiers(Qt::ControlModifier);
		}
	}

	void addressShortcuts()
	{
		QFETCH(int, source);
		QFETCH(int, key);
		QFETCH(Qt::KeyboardModifiers, modifiers);
		address_->setText("C:/Docs[1]/draft[D]");
		QWidget *sourceWidget = tree_;
		if (source == 1) sourceWidget = table_;
		if (source == 2) {
			window_->setAddressBarVisible(true);
			sourceWidget = address_;
			address_->deselect();
		}
		sourceWidget->setFocus();
		QVERIFY(sourceWidget->hasFocus());
		QTest::keyClick(sourceWidget, Qt::Key(key), modifiers);
		QVERIFY(window_->isAddressBarVisible());
		QVERIFY(address_->hasFocus());
		QCOMPARE(address_->selectedText(), address_->text());
		QCOMPARE(address_->text(), QString("C:/Docs[1]/draft[D]"));
		QCOMPARE(window_->viewmode(), MainWindow::List);
	}

	void viewShortcutsRemainAvailable()
	{
		window_->setFocusFolderTree();
		QTest::keyClicks(tree_, "]");
		QCOMPARE(window_->viewmode(), MainWindow::Thumbnail);
		window_->setFocusItemsView();
		QVERIFY(thumbnails_->hasFocus());
		QTest::keyClicks(thumbnails_, "[");
		QCOMPARE(window_->viewmode(), MainWindow::List);
		window_->setFocusItemsView();
		QVERIFY(table_->hasFocus());
		QTest::keyClicks(table_, "]");
		QCOMPARE(window_->viewmode(), MainWindow::Thumbnail);
		QVERIFY(global->incremental_search_text.isEmpty());
	}

	void addressExitKeys_data()
	{
		QTest::addColumn<int>("key");
		QTest::newRow("Escape") << int(Qt::Key_Escape);
		QTest::newRow("Tab") << int(Qt::Key_Tab);
	}

	void addressExitKeys()
	{
		QFETCH(int, key);
		window_->setAddressBarVisible(true);
		QVERIFY(address_->hasFocus());
		QTest::keyClick(address_, Qt::Key(key));
		QVERIFY(!window_->isAddressBarVisible());
		QVERIFY(tree_->hasFocus());
	}

	void filterCharacterInput_data()
	{
		QTest::addColumn<QString>("filter");
		QTest::addColumn<QString>("input");
		QTest::addColumn<QString>("expected");
		for (auto value : {0x0430, 0x0108, 0x01ff, 0x3042, 0x00a0, 0x0000, 0x007f}) {
			QTest::newRow(qPrintable(QString("U+%1").arg(value, 4, 16, QChar('0'))))
					<< QString("seed") << QString(QChar(value)) << QString("seed");
		}
		QTest::newRow("surrogate pair") << QString("seed") << QString::fromUtf8("😀") << QString("seed");
		QTest::newRow("empty input") << QString("seed") << QString() << QString("seed");
		QTest::newRow("space") << QString("seed") << QString(" ") << QString("seed");
		QTest::newRow("tab") << QString("seed") << QString("\t") << QString("seed");
		QTest::newRow("backspace") << QString("seed") << QString(QChar(incrementalsearch::ASCII_BACKSPACE)) << QString("see");
		QTest::newRow("empty backspace") << QString() << QString(QChar(incrementalsearch::ASCII_BACKSPACE)) << QString();
		QTest::newRow("legacy delete") << QString("seed") << QString(QChar(incrementalsearch::ASCII_DELETE)) << QString();
		for (int value = 0x21; value <= 0x7e; ++value) {
			QTest::newRow(qPrintable(QString("ASCII %1").arg(value)))
					<< QString("seed") << QString(QChar(value)) << QString("seed") + QChar(value);
		}
	}

	void filterCharacterInput()
	{
		QFETCH(QString, filter);
		QFETCH(QString, input);
		QFETCH(QString, expected);
		QCOMPARE(incrementalsearch::appendCharToFilterText(filter, input), expected);
	}

	void unicodeKeysDoNotChangeSearch_data()
	{
		QTest::addColumn<int>("view");
		QTest::newRow("tree") << 0;
		QTest::newRow("details") << 1;
		QTest::newRow("thumbnails") << 2;
	}

	void unicodeKeysDoNotChangeSearch()
	{
		QFETCH(int, view);
		QWidget *widget = tree_;
		if (view == 1) widget = table_;
		if (view == 2) {
			window_->setViewMode(MainWindow::Thumbnail);
			widget = thumbnails_;
		}
		widget->setFocus();
		QVERIFY(widget->hasFocus());
		QTest::keyClicks(widget, "seed");
		QCOMPARE(global->incremental_search_text, QString("seed"));
		for (QString const &text : {QString(QChar(0x0430)), QString(QChar(0x0108)), QString(QChar(0x01ff)), QString("あ"), QString::fromUtf8("😀"), QString()}) {
			QKeyEvent key(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, text);
			QApplication::sendEvent(widget, &key);
			QCOMPARE(global->incremental_search_text, QString("seed"));
		}
		QTest::keyClick(widget, Qt::Key_Backspace);
		QCOMPARE(global->incremental_search_text, QString("see"));
	}

	void unicodeKeysReachAddressEditor()
	{
		window_->setAddressBarVisible(true);
		address_->clear();
		const QString text = QString::fromUtf8("аĈあ😀");
		QKeyEvent key(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, text);
		QApplication::sendEvent(address_, &key);
		QCOMPARE(address_->text(), text);
		QVERIFY(global->incremental_search_text.isEmpty());
	}

	void hiddenFilesToggleDuringTreeSearch_data()
	{
		QTest::addColumn<bool>("initially_visible");
		QTest::addColumn<bool>("zero_matches");
		QTest::newRow("show from result") << false << false;
		QTest::newRow("hide from result") << true << false;
		QTest::newRow("show from empty result") << false << true;
		QTest::newRow("hide from empty result") << true << true;
	}

	void hiddenFilesToggleDuringTreeSearch()
	{
		QFETCH(bool, initially_visible);
		QFETCH(bool, zero_matches);
		QTemporaryDir directory(QDir::currentPath() + "/treewalker-hidden-XXXXXX");
		QVERIFY(directory.isValid());
		QDir files(directory.path());
		QVERIFY(files.mkpath("before"));
		QVERIFY(files.mkpath("target/branch/leaf"));
		QVERIFY(files.mkpath("target/.hidden"));
		for (QString const &name : {QString("visible.txt"), QString(".hidden.txt")}) {
			QFile file(files.filePath("target/" + name));
			QVERIFY(file.open(QIODevice::WriteOnly));
		}
		QAbstractItemModelTester tester(tree_->model(), QAbstractItemModelTester::FailureReportingMode::QtTest);
		auto *root = addDirectoryRoot(directory.path());
		window_->setFocusFolderTree();
		if (initially_visible) QTest::keyClicks(tree_, "\\");
		const ItemIdList target_id(files.filePath("target"));
		const ItemIdList branch_id(files.filePath("target/branch"));
		QTRY_VERIFY(tree_->itemFromIidl(target_id));
		auto *target = tree_->itemFromIidl(target_id);
		tree_->setExpanded(target, true);
		QTRY_VERIFY(tree_->itemFromIidl(branch_id));
		auto *branch = tree_->itemFromIidl(branch_id);
		tree_->setExpanded(branch, true);
		tree_->setCurrentItem(zero_matches ? target : tree_->itemFromIidl(ItemIdList(files.filePath("before"))));
		QVERIFY(tree_->currentItem());
		QTest::keyClicks(tree_, zero_matches ? "unmatched-query" : "target");
		QCOMPARE(tree_->model()->rowCount(), zero_matches ? 0 : 1);
		if (!zero_matches) QCOMPARE(tree_->currentItem()->iidl(), target_id);

		QTest::keyClicks(tree_, "\\");

		QVERIFY(global->incremental_search_text.isEmpty());
		QVERIFY(tree_->currentItem());
		QCOMPARE(tree_->currentItem()->iidl(), target_id);
		QCOMPARE(window_->currentLocation(), files.filePath("target"));
		QVERIFY(tree_->isExpanded(root));
		QVERIFY(tree_->isExpanded(tree_->itemFromIidl(target_id)));
		QVERIFY(tree_->isExpanded(tree_->itemFromIidl(branch_id)));
		QTRY_COMPARE(bool(tree_->itemFromIidl(ItemIdList(files.filePath("target/.hidden")))), !initially_visible);
		QTRY_COMPARE(window_->fileitemmodel()->rowCount(), initially_visible ? 2 : 4);
		bool hidden_file = false;
		for (int row = 0; row < window_->fileitemmodel()->rowCount(); ++row) {
			hidden_file |= window_->fileitemmodel()->index(row, 0).data(PathRole).toString() == files.filePath("target/.hidden.txt");
		}
		QCOMPARE(hidden_file, !initially_visible);
	}

	void hidingSelectedHiddenAncestor_data()
	{
		QTest::addColumn<bool>("inside_hidden");
		QTest::newRow("hidden folder") << false;
		QTest::newRow("inside hidden folder") << true;
	}

	void hidingSelectedHiddenAncestor()
	{
		QFETCH(bool, inside_hidden);
		QTemporaryDir directory(QDir::currentPath() + "/treewalker-hidden-selection-XXXXXX");
		QVERIFY(directory.isValid());
		QDir files(directory.path());
		QVERIFY(files.mkpath(".hidden/inner/leaf"));
		QVERIFY(files.mkpath("visible/branch"));
		QAbstractItemModelTester tester(tree_->model(), QAbstractItemModelTester::FailureReportingMode::QtTest);
		auto *root = addDirectoryRoot(directory.path());
		window_->setFocusFolderTree();
		QTest::keyClicks(tree_, "\\");
		QTRY_VERIFY(tree_->itemFromIidl(ItemIdList(files.filePath(".hidden"))));
		auto *hidden = tree_->itemFromIidl(ItemIdList(files.filePath(".hidden")));
		tree_->setExpanded(hidden, true);
		QTRY_VERIFY(tree_->itemFromIidl(ItemIdList(files.filePath(".hidden/inner"))));
		tree_->setCurrentItem(root);
		QTest::keyClicks(tree_, inside_hidden ? "inner" : ".hidden");
		QCOMPARE(tree_->model()->rowCount(), 1);
		QVERIFY(tree_->currentItem());

		QTest::keyClicks(tree_, "\\");

		QVERIFY(global->incremental_search_text.isEmpty());
		QVERIFY(tree_->currentItem());
		QCOMPARE(tree_->currentItem()->iidl(), ItemIdList(directory.path()));
		QCOMPARE(window_->currentLocation(), directory.path());
		QTRY_VERIFY(!tree_->itemFromIidl(ItemIdList(files.filePath(".hidden"))));
		QVERIFY(!tree_->itemFromIidl(ItemIdList(files.filePath(".hidden/inner"))));
		QTRY_COMPARE(window_->fileitemmodel()->rowCount(), 1);
	}

	void hiddenFileTogglePreservesDotNamedBookmark()
	{
		auto *root = new FolderTreeItem;
		root->setText(0, "bookmarks");
		root->setData(0, KindRole, int(Kind::ChromeBookmark));
		root->setData(0, PathRole, "//bookmark//");
		root->setData(0, IidlRole, QVariant::fromValue(ItemIdList(QString("//bookmark//"))));
		auto *child = new FolderTreeItem;
		child->setText(0, ".bookmark");
		const QString path = "//bookmark//dot";
		child->setData(0, KindRole, int(Kind::ChromeBookmark));
		child->setData(0, PathRole, path);
		child->setData(0, IidlRole, QVariant::fromValue(ItemIdList(path)));
		root->addChild(child);
		tree_->addTopLevelItem(root);
		tree_->setCurrentItem(root);
		window_->setFocusFolderTree();
		QTest::keyClicks(tree_, "\\");
		QTest::keyClicks(tree_, ".bookmark");
		QCOMPARE(tree_->currentItem(), child);

		QTest::keyClicks(tree_, "\\");

		QCOMPARE(tree_->currentItem(), child);
		QCOMPARE(window_->currentLocation(), path);
		QVERIFY(global->incremental_search_text.isEmpty());
	}
};

int runAddressInputTests(int argc, char **argv)
{
	AddressInputTests tests;
	return QTest::qExec(&tests, argc, argv);
}

#include "address_input_tests.moc"
