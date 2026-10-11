#include "NetworkDiscoveryThread.h"

#include "ItemIdList.h"
#include "WindowsFileSystemProvider.h"

#include <algorithm>
#include <chrono>


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
		using clock = std::chrono::steady_clock;
		constexpr auto emit_interval = std::chrono::milliseconds(200);

		while (1) {
			std::vector<ItemIdList> seen;
			bool changed = false;
			bool pending = false; // 未通知の変更がある
			auto last_emit = clock::now() - emit_interval;

			ItemIdList iidl = FileItem::getNetwork().idlist();
			WindowsFileSystemProvider fs(iidl);
			while (fs.fetch()) {
				if (interrupted_) break;
				FileInfo2 info = fs.fileInfo();
				if (!info) continue;
				seen.push_back(info.iidl);
				{
					// 見つかった項目から順次反映する
					std::unique_lock<std::mutex> lock(mutex_);
					if (interrupted_) break;
					auto it = std::find_if(files_.begin(), files_.end(), [&](FileInfo2 const &f){ return f.iidl == info.iidl; });
					if (it == files_.end()) {
						files_.push_back(info);
						pending = changed = true;
					} else if (it->name != info.name || it->path != info.path || it->isdir != info.isdir) {
						*it = info;
						pending = changed = true;
					}
				}
				if (pending && clock::now() - last_emit >= emit_interval) {
					last_emit = clock::now();
					pending = false;
					emit filesChanged();
				}
			}
			{
				std::unique_lock<std::mutex> lock(mutex_);
				if (interrupted_) break;
				// 今回の列挙で見つからなかった項目を取り除く
				auto removed = std::remove_if(files_.begin(), files_.end(), [&](FileInfo2 const &f){
					return std::find(seen.begin(), seen.end(), f.iidl) == seen.end();
				});
				if (removed != files_.end()) {
					files_.erase(removed, files_.end());
					pending = changed = true;
				}
				if (!changed) {
					cv_.wait_for(lock, std::chrono::seconds(5));
					if (interrupted_) break;
				}
			}
			if (pending) {
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
