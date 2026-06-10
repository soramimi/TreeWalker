#ifndef BASICFILESYSTEMPROVIDER_H
#define BASICFILESYSTEMPROVIDER_H

#include "AbstractFileSystemProvider.h"
#include <QDirIterator>
#include <memory>

class BasicFileSystemProvider : public AbstractFileSystemProvider {
private:
	struct Data {
		ItemIdList iidl;
		QString dir;
		QDir::Filters filters = QDir::AllEntries | QDir::Hidden | QDir::System | QDir::AllDirs | QDir::NoDotAndDotDot;
	} d;
	std::shared_ptr<QDirIterator> iterator;
	static QString fixPath(QString path);
public:
	BasicFileSystemProvider(ItemIdList const &iidl);
	QString currentDir() const;
	bool fetch();
	QString fileName() const;
	FileInfo2 fileInfo() const;
	bool isDir() const;
	FileSystemProviderPtr create(ItemIdList const &iidl);
	// FileInfo2 firstFileInfo() const;
	bool hasReadPermission() const;

	std::shared_ptr<AbstractFileSystemProvider> dup(ItemIdList const &iidl)
	{
		std::shared_ptr<BasicFileSystemProvider> ret = std::make_shared<BasicFileSystemProvider>(iidl);
		ret->d = d;
		return ret;
	}

	static void setFileInfo(FileInfo2 *fi, const QString &name, const QString &path);
};


#endif // BASICFILESYSTEMPROVIDER_H
