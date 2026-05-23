#include "AbstractFileSystemProvider.h"

#include <QFileIconProvider>


QIcon getIcon(QFileIconProvider const *iconprov, const FileInfo2 &info)
{
	if (!info.icon.isNull()) {
		return info.icon;
	}
	if (info.isdir) {
		return iconprov->icon(QFileIconProvider::Folder);
	} else {
		QFileInfo info_(info.path);
		if (info_.isFile()) {
			QIcon icon = iconprov->icon(info_);
			if (!icon.isNull()) {
				return icon;
			}
		}
		return iconprov->icon(QFileIconProvider::File);
	}
}
