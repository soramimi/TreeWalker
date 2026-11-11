#include "ThumbnailLoader.h"

#include <QFile>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QTest>
#include <atomic>

namespace {

QImage sample(Qt::GlobalColor color = Qt::red)
{
	QImage image(256, 128, QImage::Format_ARGB32);
	image.fill(color);
	return image;
}

struct Decoder {
	QSemaphore started;
	QSemaphore release;
	std::atomic<int> calls{0};
	std::atomic<int> active{0};
	std::atomic<int> maximum{0};
	bool succeeds = true;
	QImage load(QString const &)
	{
		int number = ++calls;
		int count = ++active;
		int old = maximum.load();
		while (count > old && !maximum.compare_exchange_weak(old, count)) {}
		started.release();
		release.acquire();
		--active;
		return succeeds ? sample(number == 1 ? Qt::red : Qt::blue) : QImage{};
	}
};

// Release blocked decoders before the loader's destructor, including on failure.
struct ReleaseDecoders {
	Decoder &decoder;
	ThumbnailLoader &loader;
	~ReleaseDecoders() { decoder.release.release(100); loader.stop(QDeadlineTimer(5000)); }
};

} // namespace

class ThumbnailLoaderTests : public QObject {
	Q_OBJECT
private slots:
	void completionIsCached_data()
	{
		QTest::addColumn<bool>("success");
		QTest::newRow("image") << true;
		QTest::newRow("failure") << false;
	}

	void completionIsCached()
	{
		QFETCH(bool, success);
		Decoder decoder;
		decoder.succeeds = success;
		ThumbnailLoader loader([&](auto const &path) { return decoder.load(path); });
		ReleaseDecoders release{decoder, loader};
		std::atomic<int> completed{0};
		connect(&loader, &ThumbnailLoader::taskDone, &loader, [&](auto entity) {
			// The completion signal must also allow reentrant cache queries.
			if (loader.query(entity->path) == entity) ++completed;
		}, Qt::DirectConnection);
		loader.start();
		loader.start();
		QVERIFY(!loader.query("image"));
		QVERIFY(decoder.started.tryAcquire(1, 5000));
		for (int i = 0; i < 100; ++i) QVERIFY(!loader.query("image"));
		QCOMPARE(decoder.calls.load(), 1);
		decoder.release.release();
		QTRY_COMPARE(completed.load(), 1);
		auto cached = loader.query("image");
		QVERIFY(cached);
		QCOMPARE(cached->image.isNull(), !success);
		QVERIFY(loader.isCurrent(cached));
		for (int i = 0; i < 100; ++i) QCOMPARE(loader.query("image"), cached);
		QCOMPARE(decoder.calls.load(), 1);
		QCOMPARE(decoder.maximum.load(), 1);
		loader.stop();
		QCOMPARE(loader.query("image"), cached);
		loader.start();
		QCOMPARE(loader.query("image"), cached);
	}

	void obsoleteResultIsDiscarded_data()
	{
		QTest::addColumn<int>("change");
		QTest::newRow("cache generation") << 0;
		QTest::newRow("file size") << 1;
		QTest::newRow("modified time") << 2;
	}

	void obsoleteResultIsDiscarded()
	{
		QFETCH(int, change);
		Decoder decoder;
		ThumbnailLoader loader([&](auto const &path) { return decoder.load(path); });
		ReleaseDecoders release{decoder, loader};
		std::atomic<int> completed{0};
		connect(&loader, &ThumbnailLoader::taskDone, &loader, [&](auto) { ++completed; }, Qt::DirectConnection);
		loader.start();
		QDateTime modified = QDateTime::fromMSecsSinceEpoch(1000);
		qint64 size = 10;
		QVERIFY(!loader.query("image", modified, size));
		QVERIFY(decoder.started.tryAcquire(1, 5000));
		if (change == 0) loader.clearCache();
		if (change == 1) ++size;
		if (change == 2) modified = modified.addSecs(1);
		QVERIFY(!loader.query("image", modified, size));
		QCOMPARE(decoder.calls.load(), 1);
		decoder.release.release();
		QVERIFY(decoder.started.tryAcquire(1, 5000));
		QCOMPARE(completed.load(), 0);
		QCOMPARE(decoder.maximum.load(), 1);
		decoder.release.release();
		QTRY_COMPARE(completed.load(), 1);
		auto cached = loader.query("image", modified, size);
		QVERIFY(cached);
		QCOMPARE(cached->image.pixelColor(0, 0), QColor(Qt::blue));
		QCOMPARE(cached->size, size);
		QCOMPARE(cached->modified, modified);
		QCOMPARE(decoder.calls.load(), 2);
	}

	void canceledRunningRequestCanBeReused()
	{
		Decoder decoder;
		ThumbnailLoader loader([&](auto const &path) { return decoder.load(path); });
		ReleaseDecoders release{decoder, loader};
		std::atomic<int> completed{0};
		connect(&loader, &ThumbnailLoader::taskDone, &loader, [&](auto) { ++completed; }, Qt::DirectConnection);
		loader.start();
		loader.query("image");
		QVERIFY(decoder.started.tryAcquire(1, 5000));
		loader.clearRequests();
		loader.query("image");
		decoder.release.release();
		QTRY_COMPARE(completed.load(), 1);
		QVERIFY(loader.query("image"));
		QCOMPARE(decoder.calls.load(), 1);
	}

	void canceledRequestDoesNotNotify()
	{
		Decoder decoder;
		ThumbnailLoader loader([&](auto const &path) { return decoder.load(path); });
		ReleaseDecoders release{decoder, loader};
		std::atomic<int> completed{0};
		connect(&loader, &ThumbnailLoader::taskDone, &loader, [&](auto) { ++completed; }, Qt::DirectConnection);
		loader.start();
		loader.query("image");
		QVERIFY(decoder.started.tryAcquire(1, 5000));
		loader.clearRequests();
		decoder.release.release();
		loader.stop(); // Joins the worker, so a missing notification needs no sleep.
		QCOMPARE(completed.load(), 0);
		QVERIFY(!loader.query("image"));
		loader.start();
		loader.query("image");
		QVERIFY(decoder.started.tryAcquire(1, 5000));
		decoder.release.release();
		QTRY_COMPARE(completed.load(), 1);
		QCOMPARE(decoder.calls.load(), 2);
	}

	void failureCanBeRetriedAndEvicted()
	{
		std::atomic<int> calls{0};
		std::atomic<int> completed{0};
		ThumbnailLoader loader([&](auto const &) { ++calls; return QImage{}; }, 2);
		connect(&loader, &ThumbnailLoader::taskDone, &loader, [&](auto) { ++completed; }, Qt::DirectConnection);
		loader.start();
		int expected = 0;
		for (QString const &path : {QString("a"), QString("b")}) {
			loader.query(path);
			++expected;
			QTRY_COMPARE(completed.load(), expected);
			QTRY_VERIFY(loader.query(path));
		}
		auto old = loader.query("a");
		QVERIFY(old);
		loader.query("c");
		QTRY_COMPARE(completed.load(), 3);
		QVERIFY(loader.query("a"));
		QVERIFY(loader.query("c"));
		QVERIFY(!loader.query("b")); // a was touched, so b was evicted.
		QTRY_COMPARE(completed.load(), 4);
		loader.clearCache();
		QVERIFY(!loader.isCurrent(old));
		QVERIFY(!loader.query("a"));
		QTRY_COMPARE(completed.load(), 5);
		QVERIFY(!loader.query("a", {}, 12));
		QTRY_COMPARE(completed.load(), 6);
		QCOMPARE(calls.load(), 6);
	}

	void restartDropsUnstartedRequests()
	{
		std::atomic<int> calls{0};
		std::atomic<int> completed{0};
		ThumbnailLoader loader([&](QString const &path) { if (path == "current") ++calls; return sample(); });
		connect(&loader, &ThumbnailLoader::taskDone, &loader, [&](auto) { ++completed; }, Qt::DirectConnection);
		loader.query("obsolete");
		loader.stop();
		loader.start();
		loader.query("current");
		QTRY_COMPARE(completed.load(), 1);
		QCOMPARE(calls.load(), 1);
	}

	void actualImageAndNonImage()
	{
		QTemporaryDir files;
		QVERIFY(files.isValid());
		const QString image = files.filePath("image.bmp");
		const QString text = files.filePath("text.txt");
		QVERIFY(sample().save(image));
		QFile file(text);
		QVERIFY(file.open(QIODevice::WriteOnly));
		file.write("not an image");
		file.close();
		ThumbnailLoader loader;
		std::atomic<int> completed{0};
		connect(&loader, &ThumbnailLoader::taskDone, &loader, [&](auto) { ++completed; }, Qt::DirectConnection);
		loader.start();
		QVERIFY(!loader.query(image));
		QVERIFY(!loader.query(text));
		const QString missing = files.filePath("missing.bmp");
		QVERIFY(!loader.query(missing));
		QTRY_COMPARE(completed.load(), 3);
		auto ready = loader.query(image);
		QVERIFY(ready && !ready->image.isNull());
		QVERIFY(ready->image.width() <= 128 && ready->image.height() <= 128);
		auto failed = loader.query(text);
		QVERIFY(failed); // Windows Shell can return the associated file icon.
#ifndef Q_OS_WIN
		QVERIFY(failed->image.isNull());
#endif
		QVERIFY(loader.query(missing)->image.isNull());
		QVERIFY(sample(Qt::blue).save(text, "BMP"));
		QVERIFY(!loader.query(text, QDateTime::fromMSecsSinceEpoch(2000), 256));
		QTRY_COMPARE(completed.load(), 4);
		auto recovered = loader.query(text, QDateTime::fromMSecsSinceEpoch(2000), 256);
		QVERIFY(recovered && !recovered->image.isNull());
	}
};

int runThumbnailLoaderTests(int argc, char **argv)
{
	ThumbnailLoaderTests tests;
	return QTest::qExec(&tests, argc, argv);
}

#include "thumbnail_loader_tests.moc"
