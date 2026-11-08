#include "AbstractFileSystemProvider.h"
#include "BasicFileSystemProvider.h"


#include <QFileIconProvider>

#ifdef Q_OS_WIN
#include "WindowsFileSystemProvider.h"
#else
#include "xdg.h"
#endif

FileSystemProviderPtr createFileSystemProvider(ItemIdList const &iidl)
{
#ifdef Q_OS_WIN
	ItemIdList location = iidl;
	if (location.type() == ItemIdList::Type::PATH) {
		QString path = location.path();
		if (path.startsWith(QString::fromLatin1(prefix_mycomputer.data(), prefix_mycomputer.size()))) {
			return std::make_shared<BasicFileSystemProvider>(ItemIdList{});
		}
		if (path.startsWith(QString::fromLatin1(prefix_itemidlist.data(), prefix_itemidlist.size()))) {
			location = ItemIdList(path);
		}
	}
	if (WindowsFileSystemProvider::isPhysicalFilesystemFolder(location)) {
		QString path = location.path();
		if (!path.isEmpty()) return std::make_shared<BasicFileSystemProvider>(path);
	}
	return std::make_shared<WindowsFileSystemProvider>(location);
#else
	return std::make_shared<BasicFileSystemProvider>(iidl);
#endif
}

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
