#include "FileTransfer.h"
#include "TransferJob.h"
#include "TransferClipboard.h"
#include "TransferDrag.h"
#include <QMessageBox>
#include <QPushButton>
#include <QTreeView>
#include <QTableView>
#include <QListView>
#include <QStandardItemModel>
#include <QDragEnterEvent>
#include <QDropEvent>
#include "FileItemModel.h"
#include <QUrl>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QTest>

class TransferTests : public QObject {
	Q_OBJECT
private slots:
	void internalDropAcrossViews_data()
	{
		QTest::addColumn<int>("view"); QTest::addColumn<int>("modifiers");
		for (int view = 0; view < 3; ++view) for (int modifiers : {0, int(Qt::ControlModifier), int(Qt::ShiftModifier)}) QTest::newRow(qPrintable(QString("%1-%2").arg(view).arg(modifiers))) << view << modifiers;
	}
	void internalDropAcrossViews()
	{
		QFETCH(int, view); QFETCH(int, modifiers);
		QTemporaryDir root; QDir dir(root.path()); QVERIFY(dir.mkdir("target"));
		QFile file(dir.filePath("data")); QVERIFY(file.open(QIODevice::WriteOnly)); file.close();
		std::unique_ptr<QAbstractItemView> widget;
		if (view == 0) widget = std::make_unique<QTreeView>();
		else if (view == 1) widget = std::make_unique<QTableView>();
		else widget = std::make_unique<QListView>();
		QStandardItemModel model; auto *folder = new QStandardItem("target");
		folder->setData(dir.filePath("target"), PathRole); folder->setData(int(Kind::Directory), KindRole); model.appendRow(folder);
		widget->setModel(&model); widget->resize(300, 200); widget->show(); QCoreApplication::processEvents();
		int confirmations = 0;
		filetransfer::DragController controller(widget.get(), view == 0, [&] { return dir.filePath("target"); }, [&](filetransfer::Request request, Qt::DropActions allowed) {
			++confirmations;
			QTimer::singleShot(0, [] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) for (auto *button : box->buttons()) if (button->text() == "Copy") { button->click(); return; } });
			if (!filetransfer::confirmDrop(&request, allowed, widget.get())) return false;
			if (request.operation != filetransfer::Operation::Copy) return false;
			return filetransfer::execute(request)[0].outcome == filetransfer::Outcome::Completed;
		});
		filetransfer::DragSession session({file.fileName()}); auto mime = session.mimeData();
		QVERIFY(!mime->hasUrls());
		QPoint point = widget->visualRect(model.index(0, 0)).center();
		QDragEnterEvent enter(point, Qt::CopyAction | Qt::MoveAction, mime.get(), Qt::LeftButton, Qt::KeyboardModifiers(modifiers));
		QApplication::sendEvent(widget->viewport(), &enter); QVERIFY(enter.isAccepted());
		QDropEvent drop(point, Qt::CopyAction | Qt::MoveAction, mime.get(), Qt::LeftButton, Qt::KeyboardModifiers(modifiers));
		QApplication::sendEvent(widget->viewport(), &drop); QVERIFY(drop.isAccepted()); QCOMPARE(confirmations, 1);
		QVERIFY(QFileInfo::exists(dir.filePath("target/data"))); QVERIFY(QFileInfo::exists(file.fileName()));
	}

	void dropAlwaysAsks_data()
	{
		QTest::addColumn<QString>("button");
		for (auto const &button : {QString("Move"), QString("Copy"), QString("Cancel"), QString("Escape"), QString("Close")}) QTest::newRow(qPrintable(button)) << button;
	}
	void dropAlwaysAsks()
	{
		QFETCH(QString, button);
		QTemporaryDir root; QDir dir(root.path()); QVERIFY(dir.mkdir("target"));
		QFile file(dir.filePath("data")); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("data"); file.close();
		filetransfer::Request request{{file.fileName()}, dir.filePath("target")};
		bool asked = false;
		QTimer timer;
		connect(&timer, &QTimer::timeout, [&] {
			auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
			if (!box) return;
			asked = true; timer.stop();
			QVERIFY(QFileInfo::exists(file.fileName())); QVERIFY(!QFileInfo::exists(dir.filePath("target/data")));
			if (button == "Escape") { QTest::keyClick(box, Qt::Key_Escape); return; }
			if (button == "Close") { box->close(); return; }
			for (auto *candidate : box->buttons()) if (candidate->text() == button) { candidate->click(); return; }
			box->reject();
		});
		timer.start(10);
		bool accepted = filetransfer::confirmDrop(&request, Qt::CopyAction | Qt::MoveAction, nullptr);
		QVERIFY(asked); QCOMPARE(accepted, button == "Move" || button == "Copy");
		if (accepted) QCOMPARE(request.operation, button == "Move" ? filetransfer::Operation::Move : filetransfer::Operation::Copy);
		QVERIFY(QFileInfo::exists(file.fileName())); QVERIFY(!QFileInfo::exists(dir.filePath("target/data")));
	}

	void clipboardFormats()
	{
		for (auto op : {filetransfer::Operation::Copy, filetransfer::Operation::Move}) {
			auto data = filetransfer::clipboardData({"/tmp/日本語 space.txt", "/tmp/other"}, op, "token");
			auto request = filetransfer::clipboardRequest(data.get());
			QCOMPARE(request.sources, (QStringList{"/tmp/日本語 space.txt", "/tmp/other"}));
			QCOMPARE(request.operation, op); QCOMPARE(filetransfer::clipboardToken(data.get()), QString("token"));
		}
		QMimeData external;
		external.setData("x-special/gnome-copied-files", "cut\nfile:///tmp/external\n");
		QCOMPARE(filetransfer::clipboardRequest(&external).operation, filetransfer::Operation::Move);
		QCOMPARE(filetransfer::clipboardRequest(&external).sources, QStringList{"/tmp/external"});
		external.setUrls({QUrl("https://example.com/file")});
		QVERIFY(filetransfer::clipboardRequest(&external).sources.isEmpty());
	}

	void copyingMovingAndConflicts()
	{
		QTemporaryDir root;
		QDir dir(root.path());
		QVERIFY(dir.mkpath("source/folder/empty")); QVERIFY(dir.mkdir("target"));
		QFile file(dir.filePath("source/folder/data"));
		QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write("payload"), 7); file.close();
		filetransfer::Request request{{dir.filePath("source/folder")}, dir.filePath("target")};
		auto results = filetransfer::execute(request);
		QCOMPARE(results[0].outcome, filetransfer::Outcome::Completed);
		QVERIFY(QFileInfo(dir.filePath("target/folder/empty")).isDir());
		QFile copy(dir.filePath("target/folder/data")); QVERIFY(copy.open(QIODevice::ReadOnly)); QCOMPARE(copy.readAll(), QByteArray("payload")); copy.close();
		QCOMPARE(filetransfer::execute(request)[0].outcome, filetransfer::Outcome::Skipped);
		filetransfer::Callbacks callbacks;
		callbacks.conflict = [](auto const &, auto const &, bool) { return filetransfer::Decision{filetransfer::Choice::Rename, "different"}; };
		QCOMPARE(filetransfer::execute(request, callbacks)[0].outcome, filetransfer::Outcome::Completed);
		callbacks.conflict = [](auto const &, auto const &, bool) { return filetransfer::Decision{filetransfer::Choice::Replace, {}}; };
		QCOMPARE(filetransfer::execute(request, callbacks)[0].outcome, filetransfer::Outcome::Completed);
		request.operation = filetransfer::Operation::Move;
		request.destination = dir.filePath("target/different");
		QCOMPARE(filetransfer::execute(request)[0].outcome, filetransfer::Outcome::Completed);
		QVERIFY(!QFileInfo::exists(dir.filePath("source/folder")));
	}
	void cancellationAndWorkerConflict()
	{
		QTemporaryDir root; QDir dir(root.path()); QVERIFY(dir.mkdir("target"));
		QFile file(dir.filePath("data")); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(QByteArray(2 * 1024 * 1024, 'x')); file.close();
		filetransfer::Request request{{file.fileName()}, dir.filePath("target")};
		bool canceled = false;
		filetransfer::Callbacks callbacks;
		callbacks.canceled = [&] { return canceled; };
		callbacks.progress = [&](auto const &, qint64) { canceled = true; };
		QCOMPARE(filetransfer::execute(request, callbacks)[0].outcome, filetransfer::Outcome::Canceled);
		QVERIFY(QFileInfo::exists(file.fileName())); QVERIFY(!QFileInfo::exists(dir.filePath("target/data")));
		QVERIFY(dir.entryList({".treewalker-*"}, QDir::AllEntries | QDir::Hidden).isEmpty());
		QCOMPARE(filetransfer::execute(request)[0].outcome, filetransfer::Outcome::Completed);
		filetransfer::Job job(request);
		QSignalSpy done(&job, &filetransfer::Job::finished);
		connect(&job, &filetransfer::Job::conflict, &job, [&](auto const &, auto const &, bool) { job.resolve({filetransfer::Choice::Skip, {}}); });
		job.start(); QTRY_COMPARE(done.count(), 1);
		auto results = qvariant_cast<filetransfer::Results>(done[0][0]); QCOMPARE(results[0].outcome, filetransfer::Outcome::Skipped);
	}

	void validation()
	{
		QTemporaryDir root;
		QVERIFY(root.isValid());
		QDir dir(root.path());
		QVERIFY(dir.mkpath("source/child"));
		QVERIFY(dir.mkdir("target"));
		QFile file(dir.filePath("source/child/data"));
		QVERIFY(file.open(QIODevice::WriteOnly)); file.close();
		filetransfer::Request request{{dir.filePath("source"), file.fileName(), dir.filePath("source")}, dir.filePath("target")};
		QVERIFY(filetransfer::validate(&request).isEmpty());
		QCOMPARE(request.sources.size(), 1);
		request.destination = dir.filePath("source/child");
		QVERIFY(!filetransfer::validate(&request).isEmpty());
		request = {{file.fileName()}, dir.filePath("source/child")};
		QVERIFY(!filetransfer::validate(&request).isEmpty());
		request = {{dir.filePath("missing")}, dir.filePath("target")};
		QVERIFY(!filetransfer::validate(&request).isEmpty());
		request = {{file.fileName()}, "//bookmark//"};
		QVERIFY(!filetransfer::validate(&request).isEmpty());
	}
};
QTEST_MAIN(TransferTests)
#include "transfer_tests.moc"
