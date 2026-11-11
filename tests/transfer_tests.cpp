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
#include <QClipboard>
#ifndef Q_OS_WIN
#include <sys/stat.h>
#endif
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QTest>

class TransferTests : public QObject {
	Q_OBJECT
private slots:
	void cancelWaitingConflict()
	{
		QTemporaryDir root; QDir dir(root.path()); QVERIFY(dir.mkdir("target"));
		QFile file(dir.filePath("data")); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("source"); file.close();
		QFile target(dir.filePath("target/data")); QVERIFY(target.open(QIODevice::WriteOnly)); target.write("target"); target.close();
		filetransfer::Job job({{file.fileName()}, dir.filePath("target"), filetransfer::Operation::Move});
		QSignalSpy done(&job, &filetransfer::Job::finished);
		connect(&job, &filetransfer::Job::conflict, &job, [&](auto const &, auto const &, bool) { job.cancel(); });
		job.start(); QTRY_COMPARE(done.count(), 1);
		auto results = qvariant_cast<filetransfer::Results>(done[0][0]); QCOMPARE(results[0].outcome, filetransfer::Outcome::Canceled);
		QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), QByteArray("source")); QVERIFY(target.open(QIODevice::ReadOnly)); QCOMPARE(target.readAll(), QByteArray("target"));
	}

	void concurrentDestinationIsNotOverwritten()
	{
		QTemporaryDir root; QDir dir(root.path()); QVERIFY(dir.mkdir("target"));
		QFile source(dir.filePath("data")); QVERIFY(source.open(QIODevice::WriteOnly)); source.write(QByteArray(2 * 1024 * 1024, 's')); source.close();
		QString destination = dir.filePath("target/data");
		bool changed = false;
		filetransfer::Callbacks callbacks;
		callbacks.progress = [&](auto const &, qint64) { if (!changed) { changed = true; QFile concurrent(destination); if (concurrent.open(QIODevice::WriteOnly)) concurrent.write("concurrent data"); } };
		filetransfer::Request request{{source.fileName()}, dir.filePath("target")};
		QCOMPARE(filetransfer::execute(request, callbacks)[0].outcome, filetransfer::Outcome::Failed);
		QFile preserved(destination); QVERIFY(preserved.open(QIODevice::ReadOnly)); QCOMPARE(preserved.readAll(), QByteArray("concurrent data")); preserved.close();
		changed = false;
		callbacks.conflict = [](auto const &, auto const &, bool) { return filetransfer::Decision{filetransfer::Choice::Replace, {}}; };
		callbacks.progress = [&](auto const &, qint64) { if (!changed) { changed = true; QFile concurrent(destination); if (concurrent.open(QIODevice::WriteOnly)) concurrent.write("new replacement data"); } };
		QCOMPARE(filetransfer::execute(request, callbacks)[0].outcome, filetransfer::Outcome::Failed);
		QVERIFY(preserved.open(QIODevice::ReadOnly)); QCOMPARE(preserved.readAll(), QByteArray("new replacement data"));
		QVERIFY(QFileInfo::exists(source.fileName()));
	}

	void sourceMutationAndCancelBatch()
	{
		QTemporaryDir root; QDir dir(root.path()); QVERIFY(dir.mkdir("target"));
		QFile file(dir.filePath("data")); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(QByteArray(2 * 1024 * 1024, 'a')); file.close();
		filetransfer::Request request{{file.fileName()}, dir.filePath("target")};
		bool changed = false;
		filetransfer::Callbacks callbacks;
		callbacks.progress = [&](auto const &, qint64) { if (!changed) { changed = true; QFile mutate(file.fileName()); if (mutate.open(QIODevice::Append)) mutate.write("changed"); } };
		auto results = filetransfer::execute(request, callbacks); QCOMPARE(results[0].outcome, filetransfer::Outcome::Failed);
		QVERIFY(QFileInfo::exists(file.fileName())); QVERIFY(!QFileInfo::exists(dir.filePath("target/data")));
		QCOMPARE(filetransfer::execute(request)[0].outcome, filetransfer::Outcome::Completed);
		QFile other(dir.filePath("other")); QVERIFY(other.open(QIODevice::WriteOnly)); other.close();
		request.sources.push_back(other.fileName());
		callbacks.progress = {};
		callbacks.conflict = [](auto const &, auto const &, bool) { return filetransfer::Decision{filetransfer::Choice::Cancel, {}}; };
		results = filetransfer::execute(request, callbacks);
		QCOMPARE(results.size(), 2); QCOMPARE(results[0].outcome, filetransfer::Outcome::Canceled); QCOMPARE(results[1].outcome, filetransfer::Outcome::Canceled);
		QVERIFY(!QFileInfo::exists(dir.filePath("target/other")));
	}
	void linksPermissionsAndPartialFailure()
	{
#ifndef Q_OS_WIN
		QTemporaryDir root; QDir dir(root.path()); QVERIFY(dir.mkpath("source/empty")); QVERIFY(dir.mkdir("target"));
		QFile file(dir.filePath("source/data")); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("contents"); file.close();
		QVERIFY(QFile::link(file.fileName(), dir.filePath("source/link")));
		QVERIFY(QFile::link(dir.filePath("source"), dir.filePath("source/cycle")));
		filetransfer::Request request{{dir.filePath("source")}, dir.filePath("target")};
		QCOMPARE(filetransfer::execute(request)[0].outcome, filetransfer::Outcome::Completed);
		QVERIFY(QFileInfo(dir.filePath("target/source/link")).isSymLink()); QVERIFY(QFileInfo(dir.filePath("target/source/cycle")).isSymLink());
		QFile blocked(dir.filePath("blocked")); QVERIFY(blocked.open(QIODevice::WriteOnly)); blocked.close();
		QVERIFY(blocked.setPermissions({}));
		request.sources = {file.fileName(), blocked.fileName()};
		auto results = filetransfer::execute(request);
		QVERIFY(blocked.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner));
		QCOMPARE(results[0].outcome, filetransfer::Outcome::Completed); QCOMPARE(results[1].outcome, filetransfer::Outcome::Failed);
		QString fifo = dir.filePath("fifo"); QVERIFY(::mkfifo(fifo.toLocal8Bit().constData(), 0600) == 0);
		request.sources = {fifo}; request.operation = filetransfer::Operation::Move;
		QVERIFY(!filetransfer::validate(&request).isEmpty());
#else
		QSKIP("POSIX links and permission test; Windows link privileges require integration testing.");
#endif
	}
	void crossFilesystemMove()
	{
#ifndef Q_OS_WIN
		QTemporaryDir source; QTemporaryDir target("/dev/shm/treewalker-transfer-XXXXXX");
		if (!target.isValid()) QSKIP("No writable /dev/shm for a real cross-filesystem move.");
		struct stat first{}, second{};
		QVERIFY(::stat(source.path().toLocal8Bit().constData(), &first) == 0); QVERIFY(::stat(target.path().toLocal8Bit().constData(), &second) == 0);
		if (first.st_dev == second.st_dev) QSKIP("Test directories are on the same filesystem.");
		QFile file(source.filePath("cross.txt")); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("cross filesystem"); file.close();
		filetransfer::Request request{{file.fileName()}, target.path(), filetransfer::Operation::Move};
		auto results = filetransfer::execute(request); QCOMPARE(results[0].outcome, filetransfer::Outcome::Completed);
		QVERIFY(!QFileInfo::exists(file.fileName())); QFile copied(target.filePath("cross.txt")); QVERIFY(copied.open(QIODevice::ReadOnly)); QCOMPARE(copied.readAll(), QByteArray("cross filesystem"));
#else
		QSKIP("Requires two Windows volumes; see integration checklist.");
#endif
	}
	void unsupportedDropStillAsks()
	{
		QTemporaryDir root; QDir dir(root.path()); QVERIFY(dir.mkdir("target")); QFile file(dir.filePath("data")); QVERIFY(file.open(QIODevice::WriteOnly)); file.close();
		filetransfer::Request request{{file.fileName()}, dir.filePath("target")};
		int prompts = 0, warnings = 0;
		QTimer timer; connect(&timer, &QTimer::timeout, [&] {
			auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (!box) return;
			if (box->windowTitle() == "Move or copy?") { ++prompts; for (auto *button : box->buttons()) if (button->text() == "Move") { button->click(); return; } }
			else { ++warnings; box->accept(); }
		}); timer.start(10);
		QVERIFY(!filetransfer::confirmDrop(&request, Qt::CopyAction, nullptr));
		QCOMPARE(prompts, 1); QCOMPARE(warnings, 1); QVERIFY(QFileInfo::exists(file.fileName())); QVERIFY(!QFileInfo::exists(dir.filePath("target/data")));
	}

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
		bool canceled = false, private_stage = true;
		filetransfer::Callbacks callbacks;
		callbacks.canceled = [&] { return canceled; };
		callbacks.progress = [&](auto const &, qint64) {
#ifndef Q_OS_WIN
			for (auto const &entry : QDir(request.destination).entryInfoList({".treewalker-*"}, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot)) {
				private_stage &= !(entry.permissions() & (QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup | QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther));
			}
#endif
			canceled = true;
		};
		QCOMPARE(filetransfer::execute(request, callbacks)[0].outcome, filetransfer::Outcome::Canceled);
		QVERIFY(private_stage);
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
int main(int argc, char **argv)
{
	QApplication app(argc, argv);
	QStringList arguments = app.arguments();
	if (arguments.value(1) == "--clipboard-probe") {
		auto request = filetransfer::clipboardRequest(app.clipboard()->mimeData());
		if (request.sources.size() != 1 || filetransfer::pathKey(request.sources[0]) != filetransfer::pathKey(arguments.value(2))) return 21;
		if ((request.operation == filetransfer::Operation::Move) != (arguments.value(3) == "move")) return 22;
		request.destination = arguments.value(4);
		auto results = filetransfer::execute(request);
		return results.size() == 1 && results[0].outcome == filetransfer::Outcome::Completed ? 0 : 23;
	}
	if (arguments.value(1) == "--clipboard-publish") {
		app.clipboard()->setMimeData(filetransfer::clipboardData({arguments.value(2)}, arguments.value(3) == "move" ? filetransfer::Operation::Move : filetransfer::Operation::Copy, "external-test").release());
		QTimer::singleShot(15000, &app, &QApplication::quit);
		return app.exec();
	}
	if (arguments.value(1) == "--drop-receiver") {
		QListView view; QStandardItemModel model;
		auto *folder = new QStandardItem("Drop files here"); folder->setData(arguments.value(3), PathRole); folder->setData(int(Kind::Directory), KindRole); model.appendRow(folder);
		view.setModel(&model); view.setGeometry(650, 0, 500, 700); view.setWindowTitle("TreeWalker Transfer Receiver");
		int confirmations = 0;
		filetransfer::DragController controller(&view, false, [&] { return arguments.value(3); }, [&](filetransfer::Request request, Qt::DropActions allowed) {
			++confirmations;
			if (confirmations != 1) { app.exit(31); return false; }
			bool accepted = filetransfer::confirmDrop(&request, allowed, &view);
			int code = 0;
			if (arguments.value(4) == "Cancel") {
				if (accepted || !QFileInfo::exists(arguments.value(2)) || QFileInfo::exists(QDir(arguments.value(3)).filePath(QFileInfo(arguments.value(2)).fileName()))) code = 32;
			} else if (!accepted) code = 33;
			else { auto results = filetransfer::execute(request); if (results.size() != 1 || results[0].outcome != filetransfer::Outcome::Completed) code = 34; }
			QTimer::singleShot(200, &app, [&, code] { app.exit(code); });
			return accepted && code == 0;
		});
		controller.allowExternalDrops(true);
		QTimer click;
		QObject::connect(&click, &QTimer::timeout, [&] {
			auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
			if (!box) return;
			if (!QFileInfo::exists(arguments.value(2))) { app.exit(35); return; }
			for (auto *button : box->buttons()) if (button->text() == arguments.value(4)) { click.stop(); button->click(); return; }
		});
		click.start(20); view.show();
		QTimer::singleShot(15000, &app, [&] { app.exit(36); });
		return app.exec();
	}
	TransferTests tests;
	return QTest::qExec(&tests, argc, argv);
}
#include "transfer_tests.moc"
