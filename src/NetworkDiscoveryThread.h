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
	std::vector<FileInfo2> files_;
public:
	NetworkDiscoveryThread();
	~NetworkDiscoveryThread();
	void start();
	void detach();
	std::vector<FileInfo2> files() const;
signals:
	void filesChanged();
};

#endif // NETWORKDISCOVERYTHREAD_H
