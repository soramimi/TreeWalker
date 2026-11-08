#include "ThumbnailLoader.h"

#include "common/misc.h"
#include <QDebug>
#include <QElapsedTimer>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <list>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#ifdef Q_OS_WIN
#include "WindowsCom.h"
#include "WindowsShellAPI.h"
#endif

template <typename KEY, typename VALUE> class T_Cache {
public:
	struct CacheItem {
		KEY key;
		VALUE value;
	};
private:
	std::list<CacheItem> items_;
	std::unordered_map<KEY, typename std::list<CacheItem>::iterator> index_;

	size_t capacity_;

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
	explicit T_Cache(size_t capacity) : capacity_(std::max(size_t(1), capacity)) {}
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
			if (items_.size() >= capacity_) {
				evict_expired(capacity_ - 1);
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
	void erase(KEY const &key)
	{
		auto it = index_.find(key);
		if (it == index_.end()) return;
		items_.erase(it->second);
		index_.erase(it);
	}
};

struct ThumbnailLoader::Private {
	std::mutex mutex;
	std::condition_variable cond;
	std::vector<std::thread> threads;
	bool interrupted = false;
	quint64 generation = 0;
	std::deque<std::shared_ptr<ThumbnailLoader::Entiry>> submission_queue;
	std::unordered_map<QString, std::shared_ptr<Entiry>> requested;
	std::unordered_map<QString, std::shared_ptr<Entiry>> running;
	T_Cache<QString, std::shared_ptr<ThumbnailLoader::Entiry>> completion_map;
	LoadFunction load;
	Private(LoadFunction load, size_t capacity)
		: completion_map(capacity), load(std::move(load)) {}
};

ThumbnailLoader::ThumbnailLoader(LoadFunction load, size_t cache_capacity)
	: m(new Private(std::move(load), cache_capacity))
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
	std::lock_guard<std::mutex> lock(m->mutex);
	if (!m->threads.empty()) return;
	m->interrupted = false;
	for (size_t i = 0; i < 8; i++) {
		m->threads.emplace_back([this]{
#ifdef Q_OS_WIN
			WindowsComApartment apartment;
			if (!apartment.initialized()) {
				qWarning() << "Cannot initialize COM for thumbnails:" << Qt::hex << static_cast<quint32>(apartment.result());
				emit failed({}, tr("Cannot load thumbnails."));
				return;
			}
			if (!WindowsShellAPI::forCurrentThread().isReady()) {
				qWarning() << "Cannot initialize Windows Shell for thumbnails.";
				emit failed({}, tr("Cannot load thumbnails."));
				return;
			}
#endif
			while (true) {
#ifdef Q_OS_WIN
				// Shell handlers can require STA callbacks, including while idle.
				if (!WindowsComApartment::pumpMessages()) return;
#endif
				std::shared_ptr<ThumbnailLoader::Entiry> entity;
				{
					std::unique_lock<std::mutex> lock(m->mutex);
					if (m->interrupted) break;
					if (m->submission_queue.empty()) {
#ifdef Q_OS_WIN
						m->cond.wait_for(lock, std::chrono::milliseconds(20));
#else
						m->cond.wait(lock);
#endif
						continue;
					}
					entity = m->submission_queue.front();
					m->submission_queue.pop_front();
					auto wanted = m->requested.find(entity->path);
					if (wanted == m->requested.end() || wanted->second != entity || m->running.count(entity->path)) continue;
					m->running[entity->path] = entity;
				}
				if (entity) {
					QImage img;
					try {
						if (m->load) {
							img = m->load(entity->path);
						} else if (!entity->path.endsWith(".icns", Qt::CaseInsensitive)) {
#ifdef _WIN32
							img = winQueryThumbnail(entity->path);
#else
							img = QImage(entity->path);
#endif
						}
					} catch (...) {
						// A failed decoder is a completed, cacheable request too.
					}
					entity->image = resizeImageForThumbnail(img);
					bool notify = false;
					{
						std::lock_guard<std::mutex> lock(m->mutex);
						m->running.erase(entity->path);
						auto wanted = m->requested.find(entity->path);
						if (!m->interrupted && wanted != m->requested.end()) {
							if (wanted->second == entity && entity->generation == m->generation) {
								entity->last_access = QDateTime::currentDateTime();
								m->completion_map.insert(entity->path, entity);
								m->requested.erase(wanted);
								notify = true;
							} else {
								m->submission_queue.push_back(wanted->second);
								m->cond.notify_all();
							}
						}
					}
					// Direct receivers may query the cache; do not hold its mutex.
					if (notify) emit taskDone(entity);
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
		m->requested.clear();
		m->submission_queue.clear();
		m->cond.notify_all();
	}
	for (auto &t : m->threads) {
		if (t.joinable()) {
			t.join();
		}
	}
	m->threads.clear();
}

std::shared_ptr<ThumbnailLoader::Entiry> ThumbnailLoader::query(QString const &path, QDateTime const &modified, qint64 size)
{
	if (path.isEmpty()) return {};

	std::lock_guard<std::mutex> lock(m->mutex);
	{
		auto opt = m->completion_map.find(path);
		if (opt && (*opt)->modified == modified && (*opt)->size == size) {
			(*opt)->last_access = QDateTime::currentDateTime();
			return *opt; // already completed
		}
		if (opt) m->completion_map.erase(path);
	}
	if (m->interrupted) return {};
	auto matches = [&](std::shared_ptr<Entiry> const &entry) {
		return entry->generation == m->generation && entry->modified == modified && entry->size == size;
	};
	auto wanted = m->requested.find(path);
	if (wanted != m->requested.end() && matches(wanted->second)) return {};
	auto running = m->running.find(path);
	if (running != m->running.end() && matches(running->second)) {
		m->requested[path] = running->second;
		return {};
	}
	auto task = std::make_shared<Entiry>();
	task->path = path;
	task->modified = modified;
	task->size = size;
	task->generation = m->generation;
	m->requested[path] = task;
	if (running == m->running.end()) m->submission_queue.push_back(task);
	m->cond.notify_all();
	return {};
}

bool ThumbnailLoader::isCurrent(std::shared_ptr<Entiry> const &entity) const
{
	if (!entity) return false;
	std::lock_guard<std::mutex> lock(m->mutex);
	auto cached = m->completion_map.find(entity->path);
	return entity->generation == m->generation && cached && *cached == entity;
}

void ThumbnailLoader::clearRequests()
{
	std::lock_guard<std::mutex> lock(m->mutex);
	m->submission_queue.clear();
	m->requested.clear();
}

void ThumbnailLoader::clearCache()
{
	std::lock_guard<std::mutex> lock(m->mutex);
	m->submission_queue.clear();
	m->requested.clear();
	m->completion_map.clear();
	++m->generation;
}



