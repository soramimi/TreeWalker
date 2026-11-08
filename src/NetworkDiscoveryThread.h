#ifndef NETWORKDISCOVERYTHREAD_H
#define NETWORKDISCOVERYTHREAD_H

#include "AbstractFileSystemProvider.h"

#include <QImage>
#include <QTimer>
#include <cstdint>
#include <functional>
#include <memory>
#include "WorkerThread.h"
#include <vector>

class NetworkDiscoveryThread : public QObject {
	Q_OBJECT
public:
	struct Entry {
		FileInfo2 info;
		QImage icon;
	};
	using Visitor = std::function<bool(Entry const &)>;
	// Enumerators call the visitor synchronously and stop when it returns false.
	// They may outlive this QObject, so captures must own their resources and must
	// not refer to GUI objects. A true result means enumeration completed.
	using Enumerator = std::function<bool(Visitor const &)>;
private:
	struct State;
	std::shared_ptr<State> state_;
	Enumerator enumerator_;
	WorkerThread thread_;
	bool started_ = false;
	QTimer notification_timer_;
	uint64_t delivered_revision_ = 0;
	static void run(std::shared_ptr<State> state, Enumerator enumerate);
public:
	explicit NetworkDiscoveryThread(Enumerator enumerate = {});
	~NetworkDiscoveryThread();
	void start();
	bool isReady() const; // 最初の列挙が完了したか
	void refresh(); // 待機中なら間隔を待たずに再列挙させる
	void stop();
	std::vector<FileInfo2> files() const;
signals:
	void filesChanged();
};

#endif // NETWORKDISCOVERYTHREAD_H
