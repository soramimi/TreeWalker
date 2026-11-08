#include "ApplicationGlobal.h"
#include "FileTableView.h"
#include "FolderTreeView.h"
#include "MainWindow.h"
#include "ThumbnailView.h"

#include <QApplication>
#include <QClipboard>
#include <QLineEdit>
#include <QMimeData>
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
};

int runAddressInputTests(int argc, char **argv)
{
	AddressInputTests tests;
	return QTest::qExec(&tests, argc, argv);
}

#include "address_input_tests.moc"
