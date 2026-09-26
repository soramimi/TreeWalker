#include "ApplicationGlobal.h"

#ifdef Q_OS_WIN
#include "WindowsShellAPI.h"

#include <QIcon>
#include <QPainter>
#include <unordered_set>
#endif


ApplicationGlobal::ApplicationGlobal()
{
#ifdef Q_OS_WIN
	shapi = std::make_shared<WindowsShellAPI>();
#endif
	{
		folder_icon = QImage(":/image/folder.png");
		zip_file_icon = QImage(":/image/zipicon.png");
	}
}

bool ApplicationGlobal::is_extension_archive_file(const QString &suffix)
{
	static std::unordered_set<QString> archive_extensions;
	if (archive_extensions.empty()) {
		static const char *exts[] = {
			"7z",
			"arj",
			"bz2",
			"cab",
			"cpio",
			"gz",
			"lha",
			"lz",
			"lzh",
			"lzma",
			"rar",
			"tar",
			"tbz",
			"tbz2",
			"tgz",
			"txz",
			"tzst",
			"xz",
			"zip",
			"zst",
		};
		for (const char *ext : exts) {
			archive_extensions.insert(QString::fromUtf8(ext));
		}
	}

	QString s = suffix.toLower();
	if (!s.isEmpty()) {
		if (s[0] == '.') {
			s = s.mid(1);
		}
		if (archive_extensions.find(s) != archive_extensions.end()) {
			return true;
		}
	}
	return false;
}

QIcon ApplicationGlobal::makeArchiveFileIcon(QString const &suffix)
{
	QString ext = suffix.toUpper();
	QPixmap pm = QPixmap::fromImage(global->zip_file_icon);
	{
		QPainter pr(&pm);
		pr.setFont(QFont("Arial", 40, QFont::Bold));
		QRect r = pr.fontMetrics().boundingRect(ext);
		r.adjust(-10, -2, 10, 2);
		r = QRect(pm.rect().width() - r.width() - 4, pm.rect().height() / 2, r.width(), r.height());
		pr.fillRect(r.translated(4, 4), Qt::black);
		pr.fillRect(r.adjusted(-1, -1, 1, 1), Qt::black);
		pr.fillRect(r.adjusted(1, 1, -1, -1), Qt::white);
		pr.setPen(Qt::black);
		pr.drawText(r, Qt::AlignCenter, ext);
	}
	return QIcon(pm);
}
