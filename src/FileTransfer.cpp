#include "FileTransfer.h"
#include "FileItemModel.h"
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <algorithm>

namespace filetransfer {

QString pathKey(QString const &path)
{
	QString key = QDir::cleanPath(QDir::fromNativeSeparators(path));
#ifdef Q_OS_WIN
	key = key.toCaseFolded();
#endif
	return key;
}

static QString entryPath(QFileInfo const &info)
{
	return QDir(info.dir().canonicalPath()).filePath(info.fileName());
}

static bool inside(QString const &child, QString const &parent)
{
	const QString p = pathKey(parent);
	const QString c = pathKey(child);
	return c == p || c.startsWith(p.endsWith('/') ? p : p + '/');
}

QString validate(Request *request)
{
	if (!request || request->sources.isEmpty()) return QObject::tr("No files selected.");
	QFileInfo directory(request->destination);
	if (!directory.isAbsolute() || !directory.isDir()) return QObject::tr("The destination is not a filesystem folder.");
	request->destination = directory.canonicalFilePath();
	QStringList sources;
	QSet<QString> seen;
	for (QString const &path : request->sources) {
		QFileInfo info(path);
		if (!info.isAbsolute() || (!info.exists() && !info.isSymLink()) || info.isRoot()) return QObject::tr("Invalid source: %1").arg(path);
		QString source = entryPath(info);
		if (info.dir().canonicalPath().isEmpty()) return QObject::tr("Cannot resolve source: %1").arg(path);
		if (seen.contains(pathKey(source))) continue;
		seen.insert(pathKey(source));
		if (info.isDir() && !info.isSymLink() && inside(request->destination, info.canonicalFilePath())) return QObject::tr("Cannot transfer a folder into itself or its descendants.");
		if (pathKey(source) == pathKey(QDir(request->destination).filePath(info.fileName()))) return QObject::tr("Source and destination are the same.");
		sources.push_back(source);
	}
	QStringList normalized;
	for (QString const &source : sources) {
		bool covered = false;
		for (QString const &parent : sources) {
			QFileInfo info(parent);
			if (source != parent && info.isDir() && !info.isSymLink() && inside(source, parent)) { covered = true; break; }
		}
		if (!covered) normalized.push_back(source);
	}
	request->sources = normalized;
	return {};
}

QStringList selectedPaths(QAbstractItemView const *view)
{
	QStringList paths;
	if (!view || !view->selectionModel()) return paths;
	for (QModelIndex const &index : view->selectionModel()->selectedRows(0)) {
		const Kind kind = static_cast<Kind>(index.data(KindRole).toInt());
		if (kind == Kind::ChromeBookmark || kind == Kind::Placeholder) return {};
		const QString path = index.data(PathRole).toString();
		QFileInfo info(path);
		if (path.isEmpty() || !info.isAbsolute() || (!info.exists() && !info.isSymLink()) || info.isRoot()) return {};
		paths.push_back(info.absoluteFilePath());
	}
	return paths;
}

QString destinationPath(QModelIndex const &index, QString const &background, bool tree, bool drop)
{
	QString path = background;
	if (tree || (drop && index.isValid())) {
		if (!index.isValid()) return {};
		const Kind kind = static_cast<Kind>(index.data(KindRole).toInt());
		if (kind == Kind::ChromeBookmark || kind == Kind::Placeholder) return {};
		path = index.data(PathRole).toString();
	}
	QFileInfo info(path);
	return info.isAbsolute() && info.isDir() ? info.canonicalFilePath() : QString();
}

}
