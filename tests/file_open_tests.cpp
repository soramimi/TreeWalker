#include "ApplicationGlobal.h"
#include "MainWindow.h"
#include "FileTableView.h"
#include "ThumbnailView.h"
#include "StatusLabel.h"
#include <QAbstractButton>
#include <QApplication>
#include <QPushButton>
#include <QDir>
#include <QDesktopServices>
#include <QFile>
#include <QKeyEvent>
#include <QMessageBox>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

class FileOpenTests : public QObject {
	Q_OBJECT
	QList<QUrl> opened_;
	QAbstractItemView *prepare(MainWindow &window, QStringList const &paths, bool thumbnail)
	{
		global->mainwindow = &window;
		window.fetchBookmarks(global->app_config_dir + "/empty-test-bookmarks.json");
		auto *table = window.findChild<FileTableView *>("tableView");
		auto *icons = window.findChild<ThumbnailView *>("thumbnailView");
		table->setModel(window.fileitemmodel()); icons->setModel(window.fileitemmodel());
		std::vector<FileItemModel::Item> items;
		for (auto const &path : paths) {
			FileItemModel::Item item;
			item.path = item.info.path = path; item.name = item.info.name = QFileInfo(path).fileName();
			item.info.iidl = ItemIdList(path); items.push_back(item);
		}
		window.fileitemmodel()->replaceItems(Kind::Directory, std::move(items), {});
		window.setViewMode(thumbnail ? MainWindow::Thumbnail : MainWindow::List);
		window.show(); window.activateWindow(); window.setFocusItemsView();
		return thumbnail ? static_cast<QAbstractItemView *>(icons) : table;
	}
	bool statusContains(MainWindow const &window, QString const &text)
	{
		for (auto *label : window.findChildren<StatusLabel *>()) if (label->text().contains(text)) return true;
		return false;
	}
private slots:
	void init()
	{
		opened_.clear(); global->incremental_search_text.clear();
		QDesktopServices::setUrlHandler("file", this, "recordUrl");
	}
	void cleanup()
	{
		QDesktopServices::unsetUrlHandler("file"); global->mainwindow = nullptr;
	}
	void recordUrl(QUrl const &url) { opened_.push_back(url); }
	void confirmedActivation_data()
	{
		QTest::addColumn<bool>("thumbnail"); QTest::addColumn<bool>("enter"); QTest::addColumn<QString>("action");
		for (bool thumbnail : {false, true}) for (bool enter : {false, true}) {
			for (QString const &action : {QString("OK"), QString("Cancel"), QString("Escape"), QString("Close"), QString("Delete"), QString("Directory"), QString("Reset"), QString("Failure")}) {
				QTest::newRow(qPrintable(QString("%1-%2-%3").arg(thumbnail ? "thumbnail" : "details", enter ? "enter" : "doubleclick", action))) << thumbnail << enter << action;
			}
		}
	}
	void confirmedActivation()
	{
		QFETCH(bool, thumbnail); QFETCH(bool, enter); QFETCH(QString, action);
		QTemporaryDir files; QVERIFY(files.isValid());
		const QString path = files.filePath("日本 語 & #%.txt");
		const QString other = files.filePath("another.txt");
		for (auto const &name : {path, other}) { QFile file(name); QVERIFY(file.open(QIODevice::WriteOnly)); }
		MainWindow window(nullptr, {}, {}, {}, [&](QUrl const &url) { opened_.push_back(url); return action != "Failure"; });
		auto *view = prepare(window, {path, other}, thumbnail);
		QTRY_VERIFY(view->hasFocus());
		QTest::keyClicks(view, "txt");
		QCOMPARE(global->incremental_search_text, QString("txt"));
		QCOMPARE(window.fileitemmodel()->rowCount(), 2);
		view->setCurrentIndex(window.fileitemmodel()->index(0, 0));
		view->selectionModel()->select(window.fileitemmodel()->index(1, 0), QItemSelectionModel::Select | QItemSelectionModel::Rows);
		const QPoint position = view->visualRect(view->currentIndex()).center();
		if (!enter) QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, position);
		const QModelIndexList selection = view->selectionModel()->selectedRows();
		int prompts = 0; bool valid_dialog = true, changed = true, timed_out = false;
		QTimer watchdog; watchdog.setInterval(2000);
		connect(&watchdog, &QTimer::timeout, [&] { timed_out = true; if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) box->reject(); }); watchdog.start();
		QTimer response; response.setInterval(10);
		connect(&response, &QTimer::timeout, [&] {
			auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
			if (!box || box->windowTitle() != "Open file") return;
			response.stop(); ++prompts;
			valid_dialog &= box->textFormat() == Qt::PlainText && box->text().contains(path) && box->text().contains(QFileInfo(path).fileName());
			valid_dialog &= box->defaultButton() == box->button(QMessageBox::Cancel) && box->escapeButton() == box->button(QMessageBox::Cancel) && opened_.isEmpty();
			if (action == "Delete" || action == "Directory") {
				changed = QFile::remove(path);
				if (action == "Directory") changed &= QDir().mkdir(path);
			}
			if (action == "Reset") {
				FileItemModel::Item item; item.path = item.info.path = other; item.name = item.info.name = "another.txt";
				window.fileitemmodel()->replaceItems(Kind::Directory, {item}, {});
				view->setCurrentIndex(window.fileitemmodel()->index(0, 0));
				// Direct reentry while the modal dialog is running must do nothing.
				window.openTableItem(view->currentIndex());
			}
			if (action == "Escape") QTest::keyClick(box, Qt::Key_Escape);
			else if (action == "Close") box->close();
			else box->button(action == "Cancel" ? QMessageBox::Cancel : QMessageBox::Ok)->click();
		}); response.start();
		if (enter) QTest::keyClick(view, Qt::Key_Return);
		else QTest::mouseDClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, position);
		QVERIFY(!timed_out); QCOMPARE(prompts, 1); QVERIFY(valid_dialog); QVERIFY(changed);
		const bool requested = action == "OK" || action == "Reset" || action == "Failure";
		QCOMPARE(opened_.size(), requested ? 1 : 0);
		if (requested) QCOMPARE(opened_[0], QUrl::fromLocalFile(path));
		if (action != "Reset") {
			QCOMPARE(global->incremental_search_text, QString("txt"));
			QCOMPARE(view->selectionModel()->selectedRows(), selection);
		}
		if (action == "Failure") QVERIFY(statusContains(window, "failed"));
		if (action == "Delete" || action == "Directory") QVERIFY(statusContains(window, "no longer"));
	}
	void repeatAndMissingFile_data()
	{
		QTest::addColumn<bool>("thumbnail");
		QTest::newRow("details") << false; QTest::newRow("thumbnail") << true;
	}
	void repeatAndMissingFile()
	{
		QFETCH(bool, thumbnail);
		QTemporaryDir files; const QString path = files.filePath("missing.txt");
		QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.close();
		MainWindow window;
		auto *view = prepare(window, {path}, thumbnail); QTRY_VERIFY(view->hasFocus());
		view->setCurrentIndex(window.fileitemmodel()->index(0, 0));
		int prompts = 0;
		QTimer response;
		connect(&response, &QTimer::timeout, [&] { if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) { ++prompts; box->reject(); } }); response.start(10);
		QKeyEvent repeat(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, QString(), true, 1);
		QApplication::sendEvent(view, &repeat);
		window.keyPressEvent(&repeat);
		QCOMPARE(prompts, 0); QVERIFY(opened_.isEmpty());
		QVERIFY(file.remove());
		QTest::keyClick(view, Qt::Key_Return);
		QCOMPARE(prompts, 0); QVERIFY(opened_.isEmpty()); QVERIFY(statusContains(window, "does not exist"));
	}
};

int runFileOpenTests(int argc, char **argv)
{
	FileOpenTests tests;
	return QTest::qExec(&tests, argc, argv);
}
#include "file_open_tests.moc"
