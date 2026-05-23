#include "ItemIdList.h"
#include "ApplicationGlobal.h"

#include "realpath.h"

#ifdef Q_OS_WIN
#include "WindowsFileSystemProvider.h"
#include "WindowsShellAPI.h"
#endif

ItemIdList::ItemIdList(const QByteArray &iidl)
{
	d.iidl = iidl;
	d.type = ItemIdList::Type::WIN_SHELL_ITEMIDLIST;
}

ItemIdList::ItemIdList(QString const &path)
{
#ifdef Q_OS_WIN
	QString path2 = path;
	path2.replace('/', '\\');
	d.iidl = global->shapi->parseDisplayName(path2);
	d.type = ItemIdList::Type::WIN_SHELL_ITEMIDLIST;
#else
	d.iidl = misc::realpath(path).toUtf8();
	d.type = ItemIdList::Type::PATH;
#endif
}

QString ItemIdList::path() const
{
#ifdef Q_OS_WIN
	if (d.iidl.size() < 2) {
		return FileItem::getDesktop().idlist().path();
	}
#else
	if (d.iidl.isEmpty()) {
		return {};
	}
#endif

	if (d.type == ItemIdList::Type::PATH) {
		return QString::fromUtf8(d.iidl);
	}

	// if (d.iidl.size() >= 2 && d.iidl[0] == '/' && d.iidl[1] == '/') {
	// 	return QString::fromUtf8(d.iidl.mid(2));
	// }

#ifdef Q_OS_WIN
	if (d.iidl[0] == 0xff && d.iidl[1] == 0xff) {
		return global->shapi->getPathFromIDList(d.iidl.mid(2));
	} else {
		return global->shapi->getPathFromIDList(d.iidl);
	}
#else
	return QString::fromUtf8(d.iidl);
#endif
}
