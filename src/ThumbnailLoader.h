#ifndef THUMBNAILLOADER_H
#define THUMBNAILLOADER_H

#include <string>
#include <memory>
#include <QImage>
#include <QObject>
#include <QDateTime>


class ThumbnailLoader : public QObject {
	Q_OBJECT
public:
	struct Entiry {
		QString path;
		QImage image;
		QDateTime last_access;
	};
private:
	struct Private;
	Private *m;
	constexpr static int icon_size = 128;
	constexpr static int max_cache_size = 5000;
	static QImage resizeImageForThumbnail(const QImage &source);
public:
	ThumbnailLoader();
	virtual ~ThumbnailLoader();
	void start();
	void stop();
	std::shared_ptr<ThumbnailLoader::Entiry> query(const QString &path);
	void clearRequests();
	void clearCache();
signals:
	void taskDone(std::shared_ptr<ThumbnailLoader::Entiry> task);
};

static inline bool operator == (ThumbnailLoader::Entiry const &l, ThumbnailLoader::Entiry const &r)
{
	return l.path == r.path;
}

#endif // THUMBNAILLOADER_H
