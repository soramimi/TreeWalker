#include "NetworkDiscoveryThread.h"

#include "ItemIdList.h"
#include "WindowsFileSystemProvider.h"


NetworkDiscoveryThread::NetworkDiscoveryThread()
{

}

NetworkDiscoveryThread::~NetworkDiscoveryThread()
{
	interrupted_ = true;
	detach();
}

void NetworkDiscoveryThread::start()
{
	thread_ = std::thread([this]() {
		while (1) {
			std::vector<FileInfo2> files;
			ItemIdList iidl = FileItem::getNetwork().idlist();
			WindowsFileSystemProvider fs(iidl);
			while (fs.fetch()) {
				if (interrupted_) break;
				FileInfo2 info = fs.fileInfo();
				files.push_back(info);
			}
			bool changed = false;
			{
				std::unique_lock<std::mutex> lock(mutex_);
				if (interrupted_) break;
				if (files != files_) {
					files_ = files;
					changed = true;
				} else {
					cv_.wait_for(lock, std::chrono::seconds(5));
					if (interrupted_) break;
				}
			}
			if (changed) {
				emit filesChanged();
			}
		}
	});
}

void NetworkDiscoveryThread::detach()
{
	{
		std::unique_lock<std::mutex> lock(mutex_);
		interrupted_ = true;
	}
	cv_.notify_all();
	if (thread_.joinable()) {
		// thread_.join();
		thread_.detach();
	}
}

std::vector<FileInfo2> NetworkDiscoveryThread::files() const
{
	std::vector<FileInfo2> ret;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		ret = files_;
	}
	return ret;
}
