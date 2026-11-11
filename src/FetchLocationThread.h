#ifndef FETCHLOCATIONTHREAD_H
#define FETCHLOCATIONTHREAD_H

#include "MainWindow.h"
#include "WorkerThread.h"
#include <QTimer>
#include <atomic>
#include <functional>

class FetchLocationThread : public QObject {
	Q_OBJECT
public:
	using StopRequested = std::function<bool()>;
	// Captures must own their resources: a blocked fetch can outlive this QObject.
	using Fetcher = std::function<LocationData(LocationData, StopRequested const &)>;
	explicit FetchLocationThread(Fetcher fetcher = {});
	~FetchLocationThread();
	LocationData data;
	void start();
	void requestInterruption();
	bool isRunning() const;
	bool isPending() const { return isRunning() || notification_timer_.isActive(); }
	bool wait(QDeadlineTimer deadline = QDeadlineTimer(1000));
	static LocationData fetch(LocationData data, StopRequested const &stop);
	static LocationData fetchDirectories(LocationData data, StopRequested const &stop);
	static constexpr int maximumConcurrentFetches = 4;
private:
	struct State;
	std::shared_ptr<State> state_;
	WorkerThread thread_;
	Fetcher fetcher_;
	QTimer notification_timer_;
	std::function<void()> pending_;
	void tryStart();
	static LocationData fetchImpl(LocationData data, StopRequested const &stop, bool directories_only);
	void deliverResult();
signals:
	void done(LocationData const &data);
	void failed(LocationData const &data, QString const &message);
};

#endif // FETCHLOCATIONTHREAD_H
