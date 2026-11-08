#include "ThumbnailLoader.h"

#include "common/misc.h"
#include <QDebug>
#include <QElapsedTimer>
#include <QPointer>
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

struct ThumbnailLoader::State {
	std::mutex mutex;
	std::condition_variable cond;
	bool interrupted = false;
	quint64 generation = 0;
	std::deque<std::shared_ptr<ThumbnailLoader::Entiry>> submission_queue;
	std::unordered_map<QString, std::shared_ptr<Entiry>> requested;
	std::unordered_map<QString, std::shared_ptr<Entiry>> running;
	T_Cache<QString, std::shared_ptr<ThumbnailLoader::Entiry>> completion_map;
	LoadFunction load;
	std::deque<std::shared_ptr<Entiry>> notifications;
	QString error;
	State(LoadFunction load, size_t capacity)
		: completion_map(capacity), load(std::move(load)) {}
};

ThumbnailLoader::ThumbnailLoader(LoadFunction load, size_t cache_capacity)
	: m(std::make_shared<State>(load, cache_capacity))
	, load_(std::move(load))
	, capacity_(cache_capacity)
{
	notification_timer_.setInterval(10);
	connect(&notification_timer_, &QTimer::timeout, this, &ThumbnailLoader::deliverResults);
}

ThumbnailLoader::~ThumbnailLoader()
{
	requestStop();
	wait(QDeadlineTimer(0));
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
	if (!threads_.empty()) return;
	if (detached_) {
		m = std::make_shared<State>(load_, capacity_);
		detached_ = false;
	}
	{
		std::lock_guard<std::mutex> lock(m->mutex);
		m->interrupted = false;
	}
	for (size_t i = 0; i < 8; ++i) {
		threads_.emplace_back();
		auto state = m;
		threads_.back().start([state] { run(state); });
	}
	notification_timer_.start();
}

void ThumbnailLoader::run(std::shared_ptr<State> m)
{
#ifdef Q_OS_WIN
	WindowsComApartment apartment;
	if (!apartment.initialized()) {
		std::lock_guard<std::mutex> lock(m->mutex);
		m->error = QStringLiteral("Cannot initialize COM for thumbnails.");
		return;
	}
	if (!WindowsShellAPI::forCurrentThread().isReady()) {
		std::lock_guard<std::mutex> lock(m->mutex);
		m->error = QStringLiteral("Cannot initialize Windows Shell for thumbnails.");
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
			{
				std::lock_guard<std::mutex> lock(m->mutex);
				m->running.erase(entity->path);
				auto wanted = m->requested.find(entity->path);
				if (!m->interrupted && wanted != m->requested.end()) {
					if (wanted->second == entity && entity->generation == m->generation) {
						entity->last_access = QDateTime::currentDateTime();
						m->completion_map.insert(entity->path, entity);
						m->requested.erase(wanted);
						m->notifications.push_back(entity);
					} else {
						m->submission_queue.push_back(wanted->second);
						m->cond.notify_all();
					}
				}
			}
		}
	}
}

void ThumbnailLoader::deliverResults()
{
	QPointer<ThumbnailLoader> alive(this);
	auto state = m;
	std::deque<std::shared_ptr<Entiry>> notifications;
	QString error;
	{
		std::lock_guard<std::mutex> lock(state->mutex);
		if (state->interrupted) return;
		notifications.swap(state->notifications);
		error.swap(state->error);
	}
	if (!error.isEmpty()) emit failed({}, error);
	for (auto const &entity : notifications) {
		if (!alive) return;
		if (state != m) break;
		{
			std::lock_guard<std::mutex> lock(state->mutex);
			if (state->interrupted) return;
		}
		if (isCurrent(entity)) emit taskDone(entity);
	}
}

void ThumbnailLoader::requestStop()
{
	notification_timer_.stop();
	std::lock_guard<std::mutex> lock(m->mutex);
	m->interrupted = true;
	m->requested.clear();
	m->submission_queue.clear();
	m->notifications.clear();
	m->cond.notify_all();
}

bool ThumbnailLoader::wait(QDeadlineTimer deadline)
{
	bool finished = true;
	for (auto &thread : threads_) finished = thread.wait(deadline) && finished;
	threads_.clear();
	detached_ = detached_ || !finished;
	return finished;
}

void ThumbnailLoader::stop(QDeadlineTimer deadline)
{
	requestStop();
	wait(deadline);
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
	m->notifications.clear();
}

void ThumbnailLoader::clearCache()
{
	std::lock_guard<std::mutex> lock(m->mutex);
	m->submission_queue.clear();
	m->requested.clear();
	m->completion_map.clear();
	m->notifications.clear();
	++m->generation;
}



