#include "FileTransfer.h"
#include "TransferJob.h"
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QTest>

class TransferTests : public QObject {
	Q_OBJECT
private slots:
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
