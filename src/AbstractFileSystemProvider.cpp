#include "AbstractFileSystemProvider.h"


#include <QFileIconProvider>

#ifdef Q_OS_WIN
#include "WindowsFileSystemProvider.h"
#else
#include "BasicFileSystemProvider.h"
#include "xdg.h"
#endif

FileInfo2 desktopFileInfo()
{
#ifdef Q_OS_WIN
	return WindowsFileSystemProvider::getFileInfo(nullptr, FileItem::getDesktop().idlist());
#else
	QString path = QString::fromStdString(xdg::get_desktop_dir());
	FileInfo2 info;
	BasicFileSystemProvider::setFileInfo(&info, "Desktop", path);
	return info;
#endif
}

FileInfo2 firstFileInfo()
{
#ifdef Q_OS_WIN
	return WindowsFileSystemProvider::getFileInfo(nullptr, FileItem::getDrives().idlist());
#else
	const QString path = "/";
	const QString name = "/";
	FileInfo2 info;
	BasicFileSystemProvider::setFileInfo(&info, name, path);
	return info;
#endif
}

#ifdef Q_OS_WIN
FileInfo2 networkFileInfo()
{
	return WindowsFileSystemProvider::getFileInfo(nullptr, FileItem::getNetwork().idlist());
}
#endif
