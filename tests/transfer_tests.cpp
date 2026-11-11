#include "FileTransfer.h"
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QTest>

class TransferTests : public QObject {
	Q_OBJECT
private slots:
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
