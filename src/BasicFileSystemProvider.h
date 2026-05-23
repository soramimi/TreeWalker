#ifndef BASICFILESYSTEMPROVIDER_H
#define BASICFILESYSTEMPROVIDER_H

#include "AbstractFileSystemProvider.h"

#include <QDirIterator>

class BasicFileSystemProvider : public AbstractFileSystemProvider {
private:
	QString dir;
	QDirIterator iterator;
	void setFileInfo(FileInfo2 *fi, const QString &name, const QString &path) const;
	static QString fixPath(QString path);
public:
//	BasicFileSystemProvider(QString const &dir);
	BasicFileSystemProvider(ItemIdList const &iidl);
	QString currentDir() const;
	bool fetch();
	QString fileName() const;
	FileInfo2 fileInfo() const;
	bool isDir() const;
	FileSystemProviderPtr create(ItemIdList const &iidl);
	FileInfo2 firstFileInfo() const;
	bool hasReadPermission() const;
};


#endif // BASICFILESYSTEMPROVIDER_H
