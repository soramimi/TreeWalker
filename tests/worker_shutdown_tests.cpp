#include "FetchLocationThread.h"
#include "ThumbnailLoader.h"
#include "worker_test_support.h"
#include <QElapsedTimer>
#include <QProcess>
#include <QSignalSpy>
#include <QTest>
#include <iostream>

class WorkerShutdownTests : public QObject {
	Q_OBJECT
private slots:
	void fetchConcurrencyIsBoundedAndCanceledQueueIsSkipped()
	{
		auto gate = std::make_shared<WorkerGate>();
		ReleaseWorkers cleanup{gate};
		std::vector<std::unique_ptr<FetchLocationThread>> blocked;
		for (int i = 0; i < FetchLocationThread::maximumConcurrentFetches; ++i) {
			auto worker = std::make_unique<FetchLocationThread>([gate](LocationData data, auto const &) {
				gate->block();
				return data;
			});
			worker->start();
			blocked.push_back(std::move(worker));
		}
		QTRY_VERIFY(gate->entered.available() == FetchLocationThread::maximumConcurrentFetches);
		std::atomic<int> obsolete_calls{0};
		for (int i = 0; i < 20; ++i) {
			FetchLocationThread obsolete([&](LocationData data, auto const &) { ++obsolete_calls; return data; });
			obsolete.start();
			QCoreApplication::processEvents();
			obsolete.requestInterruption();
		}
		std::atomic<int> latest_calls{0};
		FetchLocationThread latest([&](LocationData data, auto const &) { ++latest_calls; return data; });
		QSignalSpy done(&latest, &FetchLocationThread::done);
		latest.start();
		QCoreApplication::processEvents();
		QCOMPARE(latest_calls.load(), 0);
		QCOMPARE(obsolete_calls.load(), 0);
		gate->release.release(FetchLocationThread::maximumConcurrentFetches);
		QTRY_COMPARE(done.count(), 1);
		QCOMPARE(latest_calls.load(), 1);
		QCOMPARE(obsolete_calls.load(), 0);
	}

	void threadLocalCleanupIsBounded()
	{
		struct ExitGate {
			std::shared_ptr<WorkerGate> gate;
			~ExitGate() { gate->entered.release(); gate->release.acquire(); }
		};
		auto gate = std::make_shared<WorkerGate>();
		WorkerThread worker;
		ReleaseWorkers cleanup{gate};
		worker.start([gate] { thread_local ExitGate exit{gate}; });
		QVERIFY(gate->entered.tryAcquire(1, 5000));
		QVERIFY(worker.isRunning());
		ReleaseWatchdog watchdog(gate);
		QElapsedTimer elapsed;
		elapsed.start();
		QVERIFY(!worker.wait(QDeadlineTimer(30)));
		QVERIFY(elapsed.elapsed() < 500);
		gate->release.release();
		QVERIFY(worker.wait(QDeadlineTimer(5000)));
	}

	void blockedFetchCanOutliveNotifier()
	{
		auto gate = std::make_shared<WorkerGate>();
		auto worker = std::make_unique<FetchLocationThread>([gate](LocationData data, auto const &) {
			gate->block();
			return data;
		});
		ReleaseWorkers cleanup{gate};
		int notifications = 0;
		connect(worker.get(), &FetchLocationThread::done, this, [&](auto const &) { ++notifications; });
		worker->start();
		QVERIFY(gate->entered.tryAcquire(1, 5000));
		QElapsedTimer elapsed;
		ReleaseWatchdog watchdog(gate);
		elapsed.start();
		worker->requestInterruption();
		QVERIFY(!worker->wait(QDeadlineTimer(30)));
		worker.reset();
		QVERIFY(elapsed.elapsed() < 500);
		gate->release.release();
		QVERIFY(gate->waitAll());
		QCoreApplication::processEvents();
		QCOMPARE(notifications, 0);
	}

	void restartDoesNotDeliverOldFetch()
	{
		auto gate = std::make_shared<WorkerGate>();
		FetchLocationThread worker([gate](LocationData data, auto const &) {
			if (data.path == "old") gate->block();
			return data;
		});
		ReleaseWorkers cleanup{gate};
		QSignalSpy done(&worker, &FetchLocationThread::done);
		worker.data.path = "old";
		worker.start();
		QVERIFY(gate->entered.tryAcquire(1, 5000));
		worker.requestInterruption();
		ReleaseWatchdog watchdog(gate);
		QVERIFY(!worker.wait(QDeadlineTimer(0)));
		worker.data.path = "new";
		worker.start();
		QTRY_COMPARE(done.count(), 1);
		QCOMPARE(worker.data.path, QString("new"));
		gate->release.release();
		QVERIFY(gate->waitAll());
		QCoreApplication::processEvents();
		QCOMPARE(done.count(), 1);
	}

	void blockedThumbnailsCanRestart()
	{
		auto gate = std::make_shared<WorkerGate>();
		ThumbnailLoader loader([gate](QString const &path) {
			if (path == "old") gate->block();
			QImage image(8, 8, QImage::Format_ARGB32);
			image.fill(path == "old" ? Qt::red : Qt::blue);
			return image;
		});
		ReleaseWorkers cleanup{gate};
		QSignalSpy done(&loader, &ThumbnailLoader::taskDone);
		loader.start();
		loader.query("old");
		QVERIFY(gate->entered.tryAcquire(1, 5000));
		ReleaseWatchdog watchdog(gate);
		loader.stop(QDeadlineTimer(0));
		loader.start();
		loader.query("new");
		QTRY_COMPARE(done.count(), 1);
		QVERIFY(loader.query("new"));
		gate->release.release();
		QVERIFY(gate->waitAll());
		QCoreApplication::processEvents();
		QCOMPARE(done.count(), 1);
		QCOMPARE(loader.query("new")->image.pixelColor(0, 0), QColor(Qt::blue));
		loader.stop();
	}

	void processExitsWithBlockedWorkers()
	{
		QProcess child;
		child.start(QCoreApplication::applicationFilePath(), {"--blocked-workers-child", "-platform", "offscreen"});
		QVERIFY(child.waitForStarted(5000));
		QByteArray output;
		QElapsedTimer startup;
		startup.start();
		while (!output.contains("READY") && startup.elapsed() < 5000) {
			child.waitForReadyRead(100);
			output += child.readAllStandardOutput();
		}
		QVERIFY2(output.contains("READY"), child.readAllStandardError().constData());
		QElapsedTimer shutdown;
		shutdown.start();
		child.write("stop\n");
		QVERIFY(child.waitForFinished(3000));
		QCOMPARE(child.exitStatus(), QProcess::NormalExit);
		QCOMPARE(child.exitCode(), 0);
		QVERIFY(shutdown.elapsed() < 2500);
	}
};

int blockedWorkersChild()
{
	auto gate = std::make_shared<WorkerGate>();
	std::vector<std::unique_ptr<FetchLocationThread>> workers;
	for (int i = 0; i < 4; ++i) {
		auto worker = std::make_unique<FetchLocationThread>([gate](LocationData data, auto const &) { gate->block(); return data; });
		worker->start();
		workers.push_back(std::move(worker));
	}
	ThumbnailLoader thumbnails([gate](auto const &) { gate->block(); return QImage{}; });
	thumbnails.start();
	for (int i = 0; i < 8; ++i) thumbnails.query(QString::number(i));
	if (!gate->entered.tryAcquire(12, 5000)) return 2;
	std::cout << "READY" << std::endl;
	std::string command;
	std::getline(std::cin, command);
	const QDeadlineTimer deadline(1000);
	for (auto const &worker : workers) worker->requestInterruption();
	thumbnails.requestStop();
	for (auto const &worker : workers) worker->wait(deadline);
	thumbnails.wait(deadline);
	return 0;
}

int runWorkerShutdownTests(int argc, char **argv)
{
	WorkerShutdownTests tests;
	return QTest::qExec(&tests, argc, argv);
}

#include "worker_shutdown_tests.moc"
