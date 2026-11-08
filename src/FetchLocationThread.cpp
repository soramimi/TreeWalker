#include "BasicFileSystemProvider.h"
#include "FetchLocationThread.h"
#include <QElapsedTimer>
#include <mutex>
#include <chrono>
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
	thread_.start([state, input = std::move(input), fetcher = std::move(fetcher)]() mutable {
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
	});
	notification_timer_.start();
}

void FetchLocationThread::requestInterruption()
{
	notification_timer_.stop();
	state_->interrupted = true;
}

bool FetchLocationThread::isRunning() const { return thread_.isRunning(); }

bool FetchLocationThread::wait(QDeadlineTimer deadline)
{
	bool finished = thread_.wait(deadline);
	if (finished) deliverResult();
	return finished;
}

void FetchLocationThread::deliverResult()
{
	if (!notification_timer_.isActive() || isRunning()) return;
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
	if (stop()) return data;
	if (!data.isDir()) throw QStringLiteral("This location is not a filesystem folder.");
#ifdef Q_OS_WIN
	WindowsComApartment apartment;
	if (!apartment.initialized() || !WindowsShellAPI::forCurrentThread().isReady()) {
		throw QStringLiteral("Cannot initialize Windows Shell for folder loading.");
	}
#endif
	QElapsedTimer timer;
	timer.start();
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
		if (info && !(info.isdir && (info.name == "." || info.name == ".."))) data.files.push_back(std::move(info));
	}
	MainWindow::sortFileInfoList(&data.files);
	while (!stop() && timer.elapsed() < 300) {
#ifdef Q_OS_WIN
		if (!WindowsComApartment::pumpMessages()) break;
#endif
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	return data;
}
