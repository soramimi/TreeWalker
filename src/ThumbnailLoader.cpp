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

struct ThumbnailLoader::Private {
	std::mutex mutex;
	std::condition_variable cond;
	std::vector<std::thread> threads;
	bool interrupted = false;
	std::deque<std::shared_ptr<ThumbnailLoader::Entiry>> submission_queue;
	std::vector<std::shared_ptr<ThumbnailLoader::Entiry>> completion_vec;
	std::unordered_map<QString, std::shared_ptr<ThumbnailLoader::Entiry>> completion_map;
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

						m->completion_vec.push_back(entity);
						m->completion_map[entity->path] = entity;
						emit taskDone(entity);

						if (m->completion_vec.size() > max_cache_size) {
							std::sort(m->completion_vec.begin(), m->completion_vec.end(), [](std::shared_ptr<Entiry> const &l, std::shared_ptr<Entiry> const &r){
								return r->last_access < l->last_access; // sort by last access time, most recently used first
							});
							// evict 10% of the cache when it exceeds the limit
							for (size_t i = 0; i < max_cache_size / 10; i++) {
								auto item = m->completion_vec.back();
								m->completion_vec.pop_back();
								auto it = m->completion_map.find(item->path);
								if (it != m->completion_map.end()) {
									m->completion_map.erase(it);
								}
							}
						}
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
		auto it = m->completion_map.find(path);
		if (it != m->completion_map.end()) {
			auto ret = it->second;
			ret->last_access = QDateTime::currentDateTime();
			return ret; // already completed
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
	m->completion_vec.clear();
	m->completion_map.clear();
}



