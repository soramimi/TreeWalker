#ifndef ABSTRACTFILESYSTEMPROVIDER_H
#define ABSTRACTFILESYSTEMPROVIDER_H

#include "ItemIdList.h"
#include <QDateTime>
#include <QIcon>
#include <memory>

class QFileIconProvider;

class AbstractFileSystemProvider;

typedef std::shared_ptr<AbstractFileSystemProvider> FileSystemProviderPtr;

struct FileInfo2 {
	ItemIdList iidl;
	QString path;
	QString name;
	qint64 size = 0;
	QDateTime modified;
	QIcon icon;
	bool isdir = false;
	bool ishidden = false;

	operator bool () const
	{
#ifdef Q_OS_WIN
		return !iidl.empty();
#else
		return !path.isEmpty();
#endif
	}
};

class AbstractFileSystemProvider {
public:
	virtual QString currentDir() const = 0;
	virtual bool fetch() = 0;
	virtual QString fileName() const = 0;
	virtual FileInfo2 fileInfo() const = 0;
	virtual bool isDir() const = 0;
	virtual FileSystemProviderPtr create(ItemIdList const &iidl) = 0;
	virtual FileInfo2 desktopFileInfo() const = 0;
	virtual FileInfo2 firstFileInfo() const = 0;
	virtual bool hasReadPermission() const = 0;

	virtual std::shared_ptr<AbstractFileSystemProvider> dup(ItemIdList const &iidl) = 0;
};


#endif // ABSTRACTFILESYSTEMPROVIDER_H
