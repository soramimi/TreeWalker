#include "ThumbnailLoader.h"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>
#include <algorithm>
#include <QElapsedTimer>
#include <QDebug>
#include <chrono>
#include <unordered_map>
#include <list>
#include "common/misc.h"

template <typename KEY, typename VALUE> class T_Cache {
public:
	struct CacheItem {
		KEY key;
		VALUE value;
	};
private:
	std::list<CacheItem> items_;
	std::unordered_map<KEY, typename std::list<CacheItem>::iterator> index_;

	static constexpr size_t MAX_SIZE = 4096;
	static constexpr size_t TRIM_TARGET = 4000;

	void evict_expired(size_t target_count)
	{
		auto Erase = [&](auto it){
			index_.erase(it->key);
			items_.erase(it);
		};
		if (items_.size() > target_count) {
			// evict oldest valid entries if still over target
			size_t remove = items_.size() - target_count;
			for (size_t i = 0; i < remove; i++) {
				auto it = std::prev(items_.end());
				Erase(it);
			}
		}
	}
public:
	std::optional<VALUE> find(KEY const &name)
	{
		auto map_it = index_.find(name);
		if (map_it != index_.end()) {
			auto list_it = map_it->second;
			// move to front (most recently used)
			items_.splice(items_.begin(), items_, list_it);
			// update iterator in map after splice
			map_it->second = items_.begin();
			return list_it->value;
		}
		return std::nullopt;
	}
	void insert(KEY const &name, VALUE const &value)
	{
		auto now = misc::get_tick_count();
		auto SetItem = [&](CacheItem *item){
			item->value = value;
		};
		auto map_it = index_.find(name);
		if (map_it != index_.end()) {
			// update existing entry and move to front
			auto list_it = map_it->second;
			SetItem(&*list_it);
			items_.splice(items_.begin(), items_, list_it);
			map_it->second = items_.begin();
		} else {
			// evict if at capacity
			if (items_.size() >= MAX_SIZE) {
				evict_expired(TRIM_TARGET);
			}
			// insert new entry at front
			items_.emplace_front();
			auto list_it = items_.begin();
			list_it->key = name;
			SetItem(&*list_it);
			index_[name] = list_it;
		}
	}
	void clear()
	{
		items_.clear();
		index_.clear();
	}
};

struct ThumbnailLoader::Private {
	std::mutex mutex;
	std::condition_variable cond;
	std::vector<std::thread> threads;
	bool interrupted = false;
	std::deque<std::shared_ptr<ThumbnailLoader::Entiry>> submission_queue;
	T_Cache<QString, std::shared_ptr<ThumbnailLoader::Entiry>> completion_map;
};

ThumbnailLoader::ThumbnailLoader()
	: m(new Private)
{
}

ThumbnailLoader::~ThumbnailLoader()
{
	stop();
	delete m;
}

#ifdef _WIN32
QImage winQueryThumbnail(const QString &path);
#endif

QImage ThumbnailLoader::resizeImageForThumbnail(QImage const &source)
{
	int w = source.width();
	int h = source.height();
	if (w < 1 || h < 1) {
		return {};
	}
	QImage ret = source;
	// std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
	auto Resize = [&](int maxsize, Qt::TransformationMode mode) {
		if (w > maxsize) {
			h = h * maxsize / w;
			w = maxsize;
		} else if (h > maxsize) {
			w = w * maxsize / h;
			h = maxsize;
		}
		w = std::max(1, w);
		h = std::max(1, h);
		ret = ret.scaled(w, h, Qt::KeepAspectRatio, mode);
	};
	Resize(icon_size * 2, Qt::FastTransformation); // first, scale down to a reasonable size for better performance
	Resize(icon_size, Qt::SmoothTransformation);   // then, scale down to thumbnail size
	// std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
	// std::chrono::duration<double, std::micro> elapsed = end - start;
	// qDebug() << "resizeImageForThumbnail:" << elapsed.count() << "us";
	return ret;
}

void ThumbnailLoader::start()
{
	m->threads.resize(8);
	for (size_t i = 0; i < m->threads.size(); i++) {
		m->threads[i] = std::thread([this]{
			while (true) {
				std::shared_ptr<ThumbnailLoader::Entiry> entity;
				{
					std::unique_lock<std::mutex> lock(m->mutex);
					if (m->interrupted) break;
					if (m->submission_queue.empty()) {
						m->cond.wait(lock);
						continue;
					}
					entity = m->submission_queue.front();
					m->submission_queue.pop_front();
				}
				if (entity) {
#ifdef _WIN32
					QImage img = winQueryThumbnail(entity->path);
#else
					QImage img(entity->path);
#endif
					if (!img.isNull()) {
						entity->image = resizeImageForThumbnail(img);
						entity->last_access = QDateTime::currentDateTime();

						std::lock_guard<std::mutex> lock(m->mutex);
						m->completion_map.insert(entity->path, entity);
						emit taskDone(entity);
					}
				}
			}
		});
	}
}

void ThumbnailLoader::stop()
{
	{
		std::lock_guard<std::mutex> lock(m->mutex);
		m->interrupted = true;
		m->cond.notify_all();
	}
	for (auto &t : m->threads) {
		if (t.joinable()) {
			t.join();
		}
	}
}

std::shared_ptr<ThumbnailLoader::Entiry> ThumbnailLoader::query(QString const &path)
{
	if (path.isEmpty()) return {};

	std::shared_ptr<ThumbnailLoader::Entiry> task = std::make_shared<ThumbnailLoader::Entiry>();
	task->path = path;

	std::lock_guard<std::mutex> lock(m->mutex);
	{
		auto opt = m->completion_map.find(path);
		if (opt) {
			(*opt)->last_access = QDateTime::currentDateTime();
			return *opt; // already completed
		}
	}
	{
		for (std::shared_ptr<Entiry> const &t : m->submission_queue) {
			if (t->path == path) {
				return {}; // already being processed
			}
		}
	}
	m->submission_queue.push_back(task);
	m->cond.notify_all();
	return {};
}

void ThumbnailLoader::clearRequests()
{
	std::lock_guard<std::mutex> lock(m->mutex);
	m->submission_queue.clear();
}

void ThumbnailLoader::clearCache()
{
	std::lock_guard<std::mutex> lock(m->mutex);
	m->submission_queue.clear();
	m->completion_map.clear();
}



