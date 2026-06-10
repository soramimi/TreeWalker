#include "BasicFileSystemProvider.h"
#include <QDebug>
#include "joinpath.h"
#include "ApplicationGlobal.h"
#include "realpath.h"




BasicFileSystemProvider::BasicFileSystemProvider(ItemIdList const &iidl)
{
	d.iidl = iidl;
	d.dir = fixPath(d.iidl.path());
	iterator = std::make_shared<QDirIterator>(d.dir, d.filters);
}

QString BasicFileSystemProvider::fixPath(QString path)
{
	// if (path.startsWith("///")) {
	// 	path = path.mid(2);
	// }
	path = misc::realpath(path);
	return path;
}

QString BasicFileSystemProvider::currentDir() const
{
	return d.dir;
}

bool BasicFileSystemProvider::fetch()
{
	if (iterator->hasNext()) {
		iterator->next();
		return true;
	}
	return false;
}

QString BasicFileSystemProvider::fileName() const
{
	return iterator->fileName();
}

void BasicFileSystemProvider::setFileInfo(FileInfo2 *fi, QString const &name, QString const &path)
{
	fi->name = name;
	fi->path = path;
#ifdef Q_OS_WIN
	fi->iidl = path;
#else
	fi->iidl = fi->path;
#endif
	QFileInfo qfi(fi->path);
	fi->isdir = qfi.isDir();
	fi->size = qfi.size();
	fi->modified = qfi.lastModified();
	fi->ishidden = qfi.isHidden() || name.startsWith('.');
}

// FileInfo2 BasicFileSystemProvider::firstFileInfo() const
// {
// 	FileInfo2 fi;
// 	setFileInfo(&fi, currentDir(), currentDir());
// 	return fi;
// }

bool BasicFileSystemProvider::hasReadPermission() const
{
	QFileInfo qfi(currentDir());
	return qfi.isReadable();
}

FileInfo2 BasicFileSystemProvider::fileInfo() const
{
	FileInfo2 fi;
	setFileInfo(&fi, iterator->fileName(), iterator->filePath());
	return fi;
}

bool BasicFileSystemProvider::isDir() const
{
	return fileInfo().isdir;
}

FileSystemProviderPtr BasicFileSystemProvider::create(ItemIdList const &iidl)
{
	return FileSystemProviderPtr(new BasicFileSystemProvider(iidl));
}
