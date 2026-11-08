#ifndef WORKER_TEST_SUPPORT_H
#define WORKER_TEST_SUPPORT_H

#include <QSemaphore>
#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <vector>
#include <thread>

struct WorkerGate {
	QSemaphore entered;
	QSemaphore release;
	std::mutex mutex;
	std::vector<std::shared_future<void>> finished;
	void block()
	{
		std::promise<void> completion;
		{
			std::lock_guard<std::mutex> lock(mutex);
			finished.push_back(completion.get_future().share());
		}
		entered.release();
		release.acquire();
		completion.set_value_at_thread_exit();
	}
	bool waitAll()
	{
		std::vector<std::shared_future<void>> futures;
		{
			std::lock_guard<std::mutex> lock(mutex);
			futures = finished;
		}
		auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		for (auto const &future : futures) {
			if (future.wait_until(deadline) != std::future_status::ready) return false;
		}
		return true;
	}
};

struct ReleaseWorkers {
	std::shared_ptr<WorkerGate> gate;
	~ReleaseWorkers() { gate->release.release(100); gate->waitAll(); }
};

// A broken wait must fail the test, not hang its destructor indefinitely.
// Ordering in successful tests still comes exclusively from the semaphores.
class ReleaseWatchdog {
	std::promise<void> canceled_;
	std::thread thread_;
public:
	explicit ReleaseWatchdog(std::shared_ptr<WorkerGate> gate)
		: thread_([gate, canceled = canceled_.get_future()]() mutable {
			if (canceled.wait_for(std::chrono::seconds(2)) == std::future_status::timeout) gate->release.release(100);
		}) {}
	~ReleaseWatchdog() { canceled_.set_value(); thread_.join(); }
};

#endif // WORKER_TEST_SUPPORT_H
