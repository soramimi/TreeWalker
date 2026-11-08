#include "NetworkDiscoveryThread.h"

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QtTest>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

namespace {

struct BackendGate {
	std::mutex mutex;
	std::condition_variable cv;
	bool entered = false;
	bool released = false;
	bool returned = false;
	bool accepted_after_release = true;
	std::shared_ptr<std::atomic<bool>> destroyed;
	~BackendGate() { if (destroyed) destroyed->store(true); }
};

NetworkDiscoveryThread::Entry entry(QString name)
{
	NetworkDiscoveryThread::Entry result;
	result.info.iidl.iidl() = QByteArray("host-id");
	result.info.name = std::move(name);
	result.info.isdir = true;
	return result;
}

bool waitForEntry(std::shared_ptr<BackendGate> const &gate)
{
	std::unique_lock<std::mutex> lock(gate->mutex);
	return gate->cv.wait_for(lock, std::chrono::seconds(2), [&] { return gate->entered; });
}

bool blockAndVisit(std::shared_ptr<BackendGate> const &gate, NetworkDiscoveryThread::Visitor const &visit)
{
	{
		std::unique_lock<std::mutex> lock(gate->mutex);
		gate->entered = true;
		gate->cv.notify_all();
		gate->cv.wait(lock, [&] { return gate->released; });
	}
	const bool accepted = visit(entry("old host"));
	{
		std::lock_guard<std::mutex> lock(gate->mutex);
		gate->accepted_after_release = accepted;
		gate->returned = true;
	}
	gate->cv.notify_all();
	return accepted;
}

std::thread releaseLater(std::shared_ptr<BackendGate> gate)
{
	// This also bounds the test when a regression makes stop() wait for Shell.
	return std::thread([gate] {
		std::this_thread::sleep_for(std::chrono::milliseconds(750));
		{
			std::lock_guard<std::mutex> lock(gate->mutex);
			gate->released = true;
		}
		gate->cv.notify_all();
	});
}

} // namespace

class NetworkDiscoveryTests : public QObject {
	Q_OBJECT
private slots:
	void destructionWhileEnumerationIsBlocked()
	{
		auto destroyed = std::make_shared<std::atomic<bool>>(false);
		auto gate = std::make_shared<BackendGate>();
		gate->destroyed = destroyed;
		auto discovery = std::make_unique<NetworkDiscoveryThread>([gate](NetworkDiscoveryThread::Visitor const &visit) {
			return blockAndVisit(gate, visit);
		});
		int notifications = 0;
		connect(discovery.get(), &NetworkDiscoveryThread::filesChanged, this, [&] { ++notifications; });
		discovery->start();
		const bool entered = waitForEntry(gate);
		auto unblock = releaseLater(gate);
		QElapsedTimer timer;
		timer.start();
		discovery.reset();
		const qint64 elapsed = timer.elapsed();
		unblock.join();
		bool returned;
		bool accepted;
		{
			std::unique_lock<std::mutex> lock(gate->mutex);
			returned = gate->cv.wait_for(lock, std::chrono::seconds(2), [&] { return gate->returned; });
			accepted = gate->accepted_after_release;
		}
		gate.reset();
		QVERIFY(entered);
		QVERIFY2(elapsed < 500, "Destruction waited for the blocked enumeration");
		QVERIFY(returned);
		QVERIFY(!accepted);
		QTRY_VERIFY_WITH_TIMEOUT(destroyed->load(), 2000);
		QTest::qWait(250);
		QCOMPARE(notifications, 0);
	}

	void stopWhileWaitingAndRefresh()
	{
		auto calls = std::make_shared<std::atomic<int>>(0);
		NetworkDiscoveryThread discovery([calls](NetworkDiscoveryThread::Visitor const &visit) {
			++*calls;
			return visit(entry("host"));
		});
		QSignalSpy notifications(&discovery, &NetworkDiscoveryThread::filesChanged);
		discovery.start();
		QTRY_VERIFY_WITH_TIMEOUT(discovery.isReady(), 2000);
		QTRY_VERIFY_WITH_TIMEOUT(notifications.count() > 0, 2000);
		QCOMPARE(discovery.files().size(), size_t(1));
		discovery.refresh();
		QTRY_VERIFY_WITH_TIMEOUT(calls->load() >= 2, 2000);
		QElapsedTimer timer;
		timer.start();
		discovery.stop();
		QVERIFY(timer.elapsed() < 500);
		const int count = notifications.count();
		QTest::qWait(250);
		QCOMPARE(notifications.count(), count);
		discovery.stop(); // Repeated stop is safe.
	}

	void restartWhileOldEnumerationIsBlocked()
	{
		auto gate = std::make_shared<BackendGate>();
		auto calls = std::make_shared<std::atomic<int>>(0);
		NetworkDiscoveryThread discovery([gate, calls](NetworkDiscoveryThread::Visitor const &visit) {
			if (++*calls == 1) return blockAndVisit(gate, visit);
			return visit(entry("new host"));
		});
		discovery.start();
		const bool entered = waitForEntry(gate);
		auto unblock = releaseLater(gate);
		QElapsedTimer timer;
		timer.start();
		discovery.stop();
		const qint64 elapsed = timer.elapsed();
		discovery.start();
		QElapsedTimer ready_timer;
		ready_timer.start();
		while (!discovery.isReady() && ready_timer.elapsed() < 2000) QTest::qWait(10);
		const bool ready = discovery.isReady();
		const auto files = discovery.files();
		unblock.join();
		bool returned;
		bool accepted;
		{
			std::unique_lock<std::mutex> lock(gate->mutex);
			returned = gate->cv.wait_for(lock, std::chrono::seconds(2), [&] { return gate->returned; });
			accepted = gate->accepted_after_release;
		}
		discovery.stop();
		QVERIFY(entered);
		QVERIFY(elapsed < 500);
		QVERIFY(ready);
		QVERIFY(returned);
		QVERIFY(!accepted);
		QCOMPARE(files.size(), size_t(1));
		QCOMPARE(files.front().name, QString("new host"));
		QCOMPARE(discovery.files().front().name, QString("new host"));
	}
};

int runNetworkDiscoveryTests(int argc, char **argv)
{
	NetworkDiscoveryTests tests;
	return QTest::qExec(&tests, argc, argv);
}

#include "network_discovery_tests.moc"
