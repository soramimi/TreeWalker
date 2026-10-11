#ifndef NETWORKDISCOVERYTHREAD_H
#define NETWORKDISCOVERYTHREAD_H

#include "AbstractFileSystemProvider.h"

#include <thread>
#include <mutex>
#include <condition_variable>

class NetworkDiscoveryThread : public QObject {
	Q_OBJECT
private:
	std::thread thread_;
	mutable std::mutex mutex_;
	std::condition_variable cv_;
	bool interrupted_ = false;
	bool refresh_requested_ = false;
	std::vector<FileInfo2> files_;
public:
	NetworkDiscoveryThread();
	~NetworkDiscoveryThread();
	void start();
	void refresh(); // 待機中なら間隔を待たずに再列挙させる
	void detach();
	std::vector<FileInfo2> files() const;
signals:
	void filesChanged();
};

#endif // NETWORKDISCOVERYTHREAD_H
