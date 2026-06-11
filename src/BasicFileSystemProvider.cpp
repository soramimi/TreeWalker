#include "BasicFileSystemProvider.h"
#include <QDebug>
#include "joinpath.h"
#include "ApplicationGlobal.h"
#include "realpath.h"
#include "xdg.h"




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

FileInfo2 BasicFileSystemProvider::desktopFileInfo() const
{
	QString path = QString::fromStdString(xdg::get_desktop_dir());
	FileInfo2 info;
	BasicFileSystemProvider::setFileInfo(&info, "Desktop", path);
	return info;
}

FileInfo2 BasicFileSystemProvider::firstFileInfo() const
{
	const QString path = "/";
	const QString name = "/";
	FileInfo2 info;
	setFileInfo(&info, name, path);
	return info;
}

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
