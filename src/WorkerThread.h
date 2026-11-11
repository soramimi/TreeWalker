#ifndef WORKERTHREAD_H
#define WORKERTHREAD_H

#include <QDeadlineTimer>
#include <algorithm>
#include <chrono>
#include <future>
#include <thread>

// Only functions owning their state may run here. Completion includes thread-
// local destruction, so a ready future can be joined without waiting on COM.
class WorkerThread {
	std::thread thread_;
	std::shared_future<void> finished_;
public:
	WorkerThread() = default;
	WorkerThread(WorkerThread &&) noexcept = default;
	WorkerThread &operator=(WorkerThread &&other) noexcept
	{
		if (this == &other) return *this;
		if (thread_.joinable()) thread_.detach();
		thread_ = std::move(other.thread_);
		finished_ = std::move(other.finished_);
		return *this;
	}
	~WorkerThread() { if (thread_.joinable()) thread_.detach(); }
	template <typename Function> void start(Function function)
	{
		wait(QDeadlineTimer(0));
		std::packaged_task<void()> task(std::move(function));
		finished_ = task.get_future().share();
		thread_ = std::thread([task = std::move(task)]() mutable { task.make_ready_at_thread_exit(); });
	}
	bool isRunning() const
	{
		return finished_.valid() && finished_.wait_for(std::chrono::nanoseconds(0)) != std::future_status::ready;
	}
	std::shared_future<void> completion() const { return finished_; }
	bool wait(QDeadlineTimer deadline)
	{
		bool ready = true;
		if (finished_.valid()) {
			if (deadline.isForever()) finished_.wait();
			else ready = finished_.wait_for(std::chrono::nanoseconds(std::max(qint64(0), deadline.remainingTimeNSecs()))) == std::future_status::ready;
		}
		if (thread_.joinable()) {
			if (ready) thread_.join();
			else thread_.detach();
		}
		return ready;
	}
};

#endif // WORKERTHREAD_H
