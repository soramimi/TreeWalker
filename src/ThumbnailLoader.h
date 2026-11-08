#ifndef THUMBNAILLOADER_H
#define THUMBNAILLOADER_H

#include <QDateTime>
#include <QImage>
#include <QObject>
#include <memory>
#include <functional>
#include <string>
#include <QTimer>
#include "WorkerThread.h"
#include <vector>

class ThumbnailLoader : public QObject {
	Q_OBJECT
public:
	using LoadFunction = std::function<QImage(QString const &)>;
	struct Entiry {
		QString path;
		QImage image;
		QDateTime last_access;
		QDateTime modified;
		qint64 size = -1;
		quint64 generation = 0;
	};
private:
	struct State;
	std::shared_ptr<State> m;
	std::vector<WorkerThread> threads_;
	LoadFunction load_;
	size_t capacity_;
	bool detached_ = false;
	QTimer notification_timer_;
	static void run(std::shared_ptr<State> m);
	void deliverResults();
	constexpr static int icon_size = 128;
	static QImage resizeImageForThumbnail(const QImage &source);
public:
	explicit ThumbnailLoader(LoadFunction load = {}, size_t cache_capacity = 4096);
	virtual ~ThumbnailLoader();
	void start();
	void requestStop();
	bool wait(QDeadlineTimer deadline);
	void stop(QDeadlineTimer deadline = QDeadlineTimer(1000));
	std::shared_ptr<ThumbnailLoader::Entiry> query(const QString &path, QDateTime const &modified = {}, qint64 size = -1);
	bool isCurrent(std::shared_ptr<Entiry> const &entity) const;
	void clearRequests();
	void clearCache();
signals:
	void taskDone(std::shared_ptr<ThumbnailLoader::Entiry> task);
	void failed(QString const &path, QString const &message);
};

static inline bool operator == (ThumbnailLoader::Entiry const &l, ThumbnailLoader::Entiry const &r)
{
	return l.path == r.path;
}

#endif // THUMBNAILLOADER_H
