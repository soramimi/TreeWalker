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
	bool wait(QDeadlineTimer deadline = QDeadlineTimer(1000));
	static LocationData fetch(LocationData data, StopRequested const &stop);
private:
	struct State;
	std::shared_ptr<State> state_;
	WorkerThread thread_;
	Fetcher fetcher_;
	QTimer notification_timer_;
	void deliverResult();
signals:
	void done(LocationData const &data);
	void failed(LocationData const &data, QString const &message);
};

#endif // FETCHLOCATIONTHREAD_H
