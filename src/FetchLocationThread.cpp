#include "BasicFileSystemProvider.h"
#include "FetchLocationThread.h"
#include <mutex>
#include <chrono>
#include <vector>
#ifdef Q_OS_WIN
#include "WindowsCom.h"
#include "WindowsShellAPI.h"
#endif

struct FetchLocationThread::State {
	std::atomic<bool> interrupted{false};
	std::mutex mutex;
	LocationData result;
	QString error;
};

FetchLocationThread::FetchLocationThread(Fetcher fetcher)
	: state_(std::make_shared<State>())
	, fetcher_(fetcher ? std::move(fetcher) : Fetcher(fetch))
{
	notification_timer_.setInterval(10);
	connect(&notification_timer_, &QTimer::timeout, this, &FetchLocationThread::deliverResult);
}

FetchLocationThread::~FetchLocationThread()
{
	requestInterruption();
	wait(QDeadlineTimer(0));
}

void FetchLocationThread::start()
{
	if (isRunning() && !state_->interrupted) return;
	state_ = std::make_shared<State>();
	auto state = state_;
	auto input = data;
	auto fetcher = fetcher_;
	pending_ = [state, input = std::move(input), fetcher = std::move(fetcher)]() mutable {
		LocationData result;
		QString error;
		try {
			result = fetcher(std::move(input), [state] { return state->interrupted.load(); });
		} catch (QString const &message) {
			error = message;
		} catch (std::exception const &exception) {
			error = QString::fromUtf8(exception.what());
		} catch (...) {
			error = QStringLiteral("Cannot load this folder.");
		}
		std::lock_guard<std::mutex> lock(state->mutex);
		state->result = std::move(result);
		state->error = std::move(error);
	};
	tryStart();
	notification_timer_.start();
}

void FetchLocationThread::tryStart()
{
	if (!pending_) return;
	// Futures become ready only after thread-local COM resources are released.
	// Retain reservations even when a notifier is destroyed or restarted.
	static std::mutex mutex;
	static std::vector<std::shared_future<void>> active;
	std::lock_guard<std::mutex> lock(mutex);
	active.erase(std::remove_if(active.begin(), active.end(), [](auto const &future) {
		return future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
	}), active.end());
	if (active.size() >= maximumConcurrentFetches) return;
	thread_.start(std::move(pending_));
	pending_ = {};
	active.push_back(thread_.completion());
}

void FetchLocationThread::requestInterruption()
{
	notification_timer_.stop();
	state_->interrupted = true;
	pending_ = {};
}

bool FetchLocationThread::isRunning() const { return pending_ || thread_.isRunning(); }

bool FetchLocationThread::wait(QDeadlineTimer deadline)
{
	while (pending_) {
		tryStart();
		if (!pending_) break;
		if (deadline.hasExpired()) return false;
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	bool finished = thread_.wait(deadline);
	if (finished) deliverResult();
	return finished;
}

void FetchLocationThread::deliverResult()
{
	tryStart();
	if (!notification_timer_.isActive() || isRunning()) return;
	thread_.wait(QDeadlineTimer(0));
	notification_timer_.stop();
	if (state_->interrupted) return;
	QString error;
	{
		std::lock_guard<std::mutex> lock(state_->mutex);
		error = state_->error;
		if (error.isEmpty()) data = std::move(state_->result);
	}
	const LocationData delivered = data;
	if (error.isEmpty()) emit done(delivered);
	else emit failed(delivered, error);
}

LocationData FetchLocationThread::fetch(LocationData data, StopRequested const &stop)
{
	return fetchImpl(std::move(data), stop, false);
}

LocationData FetchLocationThread::fetchDirectories(LocationData data, StopRequested const &stop)
{
	return fetchImpl(std::move(data), stop, true);
}

LocationData FetchLocationThread::fetchImpl(LocationData data, StopRequested const &stop, bool directories_only)
{
	if (stop()) return data;
	if (!data.isDir()) throw QStringLiteral("This location is not a filesystem folder.");
#ifdef Q_OS_WIN
	WindowsComApartment apartment;
	if (!apartment.initialized() || !WindowsShellAPI::forCurrentThread().isReady()) {
		throw QStringLiteral("Cannot initialize Windows Shell for folder loading.");
	}
#endif
	auto fs = createFileSystemProvider(data.iidl);
	if (stop()) return data;
	if (!fs->hasReadPermission()) {
		data.kind = Kind::NotPermittedDirectory;
		return data;
	}
	while (!stop() && fs->fetch()) {
#ifdef Q_OS_WIN
		if (!WindowsComApartment::pumpMessages()) return data;
#endif
		if (stop()) return data;
		auto info = fs->fileInfo();
		if (directories_only && !info.isdir) continue;
		if (info && !(info.isdir && (info.name == "." || info.name == ".."))) data.files.push_back(std::move(info));
	}
	MainWindow::sortFileInfoList(&data.files);

	return data;
}
