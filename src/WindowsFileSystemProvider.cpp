#include "WindowsFileSystemProvider.h"

#include "WindowsShellAPI.h"
#include "ApplicationGlobal.h"

#include <QFileInfo>
#include <commoncontrols.h>
#include <shlwapi.h>
#include "ApplicationGlobal.h"

static inline WindowsShellAPI *shapi()
{
	return global->shapi.get();
}

bool WindowsFileSystemProvider::isPhysicalFilesystemFolder(void const *iidl)
{
	IShellItem *item = nullptr;

	HRESULT hr;

	hr = SHCreateItemFromIDList(
				(ITEMIDLIST const *)iidl,
				IID_PPV_ARGS(&item));

	if (FAILED(hr)) return false;

	SFGAOF attrs = 0;

	hr = item->GetAttributes(
				SFGAO_FOLDER | SFGAO_FILESYSTEM,
				&attrs);

	if (FAILED(hr)) return false;

	return (attrs & SFGAO_FOLDER) && (attrs & SFGAO_FILESYSTEM);
}

bool WindowsFileSystemProvider::isPhysicalFilesystemFolder(ItemIdList const &iidl)
{
	if (iidl.type() == ItemIdList::Type::PATH) {
		QString path = iidl.path();
		if (path.startsWith("//") && path.indexOf("//", 2) > 2) {
			return false;
		}
		QFileInfo qfi(path);
		return qfi.isDir() && !qfi.isSymLink();
	}
	if (iidl.size() < 2) return false;
	if (iidl[0] == '/' && iidl[1] == '/') {
		return true;
	}
	return isPhysicalFilesystemFolder((ITEMIDLIST *)iidl.data());
}

// FileItem

struct FileItem::Data {
	ItemIdList idlist;
	IShellFolder *shfolder = nullptr;
	~Data()
	{
		if (shfolder) {
			shfolder->Release();
		}
	}
};

FileItem::FileItem(FileItem::Data *p)
{
	dataptr = std::shared_ptr<Data>(p);
}

FileItem::FileItem()
{
}

FileItem::operator bool() const
{
	return dataptr && dataptr->shfolder && !dataptr->idlist.empty();
}

ItemIdList FileItem::idlist() const
{
	if (dataptr && !dataptr->idlist.empty()) {
		return dataptr->idlist;
	}
	return {};
}

IShellFolder *FileItem::shfolder() const
{
	if (dataptr) {
		return dataptr->shfolder;
	}
	return nullptr;
}

FileItem FileItem::getDesktop()
{
	Data *p = new Data;
	p->idlist = WindowsShellAPI::getSpecialFolderLocation(CSIDL_DESKTOP);
	SHGetDesktopFolder(&p->shfolder);
	return FileItem(p);
}

FileItem FileItem::getDrives()
{
	Data *p = new Data;
	p->idlist = WindowsShellAPI::getSpecialFolderLocation(CSIDL_DRIVES);
	SHGetDesktopFolder(&p->shfolder);
	return FileItem(p);
}

FileItem FileItem::getNetwork()
{
	Data *p = new Data;
	p->idlist = WindowsShellAPI::getSpecialFolderLocation(CSIDL_NETWORK);
	SHGetDesktopFolder(&p->shfolder);
	return FileItem(p);
}

FileItem FileItem::getKnownFolder(const KNOWNFOLDERID_ &id)
{
	Data *p = new Data;
	p->idlist = shapi()->getKnownFolderIDList(reinterpret_cast<KNOWNFOLDERID const &>(id));
	p->shfolder = shapi()->getShellFolder(p->idlist.d.iidl);
	return FileItem(p);
}

FileItem FileItem::parseFolder(const QString &path)
{
	Data *p = new Data;
	p->idlist = shapi()->parseDisplayName(path);
	p->shfolder = shapi()->getShellFolder(p->idlist.d.iidl);
	return FileItem(p);
}

FileItem FileItem::fromITEMIDLIST(const ITEMIDLIST_ *idl)
{
	Data *p = new Data;
	p->idlist = WindowsShellAPI::getByteArrayFromITEMIDLIST(reinterpret_cast<ITEMIDLIST const *>(idl));
	p->shfolder = shapi()->getShellFolder(p->idlist.d.iidl);
	return FileItem(p);
}

FileItem FileItem::fromITEMIDLIST(ItemIdList const &idl)
{
	Data *p = new Data;
	if (!idl.empty()) {
		p->idlist = WindowsShellAPI::getByteArrayFromITEMIDLIST((ITEMIDLIST const *)idl.data());
		p->shfolder = shapi()->getShellFolder(p->idlist.d.iidl);
	}
	return FileItem(p);
}

// WindowsFileSystemProvider

struct WindowsFileSystemProvider::Private {
	QString dir;
	QList<ItemIdList> children;
	int children_pos = -1;
	FileItem fileitem;
	FileInfo2 fileinfo;
	HIMAGELIST hImageList = nullptr;
};

WindowsFileSystemProvider::WindowsFileSystemProvider(const QString &dir)
	: m(new Private)
{
	setDir(dir);
	updateImageList();
}

WindowsFileSystemProvider::WindowsFileSystemProvider(ItemIdList const &iidl)
	: m(new Private)
{
	m->fileitem = FileItem::fromITEMIDLIST(iidl.iidl());
	updateImageList();
}

WindowsFileSystemProvider::~WindowsFileSystemProvider()
{
	delete m;
}

void WindowsFileSystemProvider::updateImageList()
{
	m->hImageList = WindowsShellAPI::getImageList(m->fileitem.idlist().d.iidl);
}

void WindowsFileSystemProvider::setDir(const QString &d)
{
	m->dir = d;
	m->dir.replace('/', '\\');
	m->fileitem = m->dir.isEmpty() ? FileItem::getDesktop() : FileItem::parseFolder(m->dir);
}

QString WindowsFileSystemProvider::currentDir() const
{
	return m->dir;
}

bool WindowsFileSystemProvider::fetch()
{
	m->fileinfo = FileInfo2();
	if (m->children.isEmpty()) {
		m->children_pos = -1;
		if (m->fileitem) {
			QList<QByteArray> list = global->shapi->enumChildren(m->fileitem.shfolder(), m->fileitem.idlist().d.iidl);
			for (const QByteArray &ba : list) {
				m->children.append(ItemIdList{ba});
			}
		} else {
			return false;
		}
	}

	if (m->children_pos < m->children.size()) {
		m->children_pos++;
	}
	if (m->children_pos >= 0 && m->children_pos < m->children.size()) {
		m->fileinfo = getFileInfo(m->hImageList, m->children[m->children_pos]);
		return true;
	}
	return false;
}

QString WindowsFileSystemProvider::fileName() const
{
	return m->fileinfo.name;
}

FileInfo2 WindowsFileSystemProvider::fileInfo() const
{
	return m->fileinfo;
}

bool WindowsFileSystemProvider::isDir() const
{
	return m->fileinfo.isdir;
}

static QImage convertHIconToQImage(HICON hIcon)
{
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
	QPixmap pm = QtWin::fromHICON(hIcon);
	return pm.toImage();
#else
	return QImage::fromHICON(hIcon);
#endif
}

QImage WindowsFileSystemProvider::getIcon(HIMAGELIST_ hImageList, void const *iidl)
{
	SHFILEINFO sfinfo{};
	// UINT flags = SHGFI_PIDL | SHGFI_SMALLICON | SHGFI_SYSICONINDEX | SHGFI_DISPLAYNAME | SHGFI_ATTRIBUTES;
	UINT flags = SHGFI_PIDL | SHGSI_ICON | SHGSI_LARGEICON | SHGFI_SYSICONINDEX;
	SHGetFileInfo((wchar_t const *)iidl, 0, &sfinfo, sizeof(SHFILEINFO), flags);
	HICON hIcon = ImageList_GetIcon(reinterpret_cast<HIMAGELIST>(hImageList), sfinfo.iIcon, ILD_NORMAL);
	QImage im = convertHIconToQImage(hIcon);
	DestroyIcon(hIcon);
	return im;
}

QImage WindowsFileSystemProvider::getStockFolderIcon()
{
	QImage ret;
#if 0
	// 普通のアイコン
	SHSTOCKICONINFO sii = {};
	sii.cbSize = sizeof(sii);
	UINT flags = SHGSI_ICON | SHGSI_LARGEICON;
	HRESULT hr = SHGetStockIconInfo(SIID_FOLDER, flags, &sii);
	if (SUCCEEDED(hr)) {
		ret = QImage::fromHICON(sii.hIcon);
		// ret = convertHIconToImage(sii.hIcon);
	}
	DestroyIcon(sii.hIcon);
#else
	// 特大アイコン

	static QImage static_image;

	if (static_image.isNull()) {
		HRESULT hr;

		SHSTOCKICONINFO sii = {};
		sii.cbSize = sizeof(sii);

		hr = SHGetStockIconInfo(
			SIID_FOLDER,
			SHGSI_SYSICONINDEX,
			&sii);

		IImageList *imageList = nullptr;

		hr = SHGetImageList(
			SHIL_JUMBO,
			IID_PPV_ARGS(&imageList));

		HICON hIcon = nullptr;

		imageList->GetIcon(
			sii.iSysImageIndex,
			ILD_TRANSPARENT,
			&hIcon);

		static_image = QImage::fromHICON(hIcon);

		DestroyIcon(hIcon);
	}

	ret = static_image;
#endif
	return ret;
}

FileInfo2 WindowsFileSystemProvider::getFileInfo(HIMAGELIST_ hImageList, ItemIdList const &iidl)
{
	if (iidl.d.iidl.size() < 2) return {};

	FileInfo2 fi;

	SHFILEINFO sfinfo;
	ITEMIDLIST const *iidl_;
	{
		char const *p = (char const *)iidl.data();
		if (p[0] == 0xff && p[1] == 0xff) {
			p += 2;
		}
		iidl_ = reinterpret_cast<ITEMIDLIST const *>(p);
	}

	SHGetFileInfo((wchar_t const *)iidl_, 0, &sfinfo, sizeof(SHFILEINFO), SHGFI_PIDL | SHGFI_DISPLAYNAME | SHGFI_ATTRIBUTES);

	fi.path = WindowsShellAPI::getPathFromIDList(iidl_);
	if (fi.path.isEmpty() && !(sfinfo.dwAttributes & SFGAO_FOLDER)) {
		return FileInfo2();
	}

	QFileInfo qfi(fi.path);

	fi.iidl = iidl.d.iidl;
	fi.name = QString::fromUtf16((ushort const *)sfinfo.szDisplayName);
	fi.isdir = fi.path.isEmpty() || qfi.isDir();
	if (isPhysicalFilesystemFolder(iidl_)) {
		// 物理ファイルシステムのフォルダはストックアイコンを使用する。
		// （プレビューつきフォルダアイコンを生成させないため）
		fi.icon = QIcon(QPixmap::fromImage(getStockFolderIcon()));
	} else {
		// 通常ファイルと仮想フォルダはシェルAPIからアイコンを取得する。
		fi.icon = QIcon(QPixmap::fromImage(getIcon(hImageList, iidl_)));
	}
	fi.size = qfi.size();
	fi.modified = qfi.lastModified();
	if (fi.modified.isValid()) {
		QDateTime dt = fi.modified.toUTC();
		if (dt.date().year() == 1601 && dt.date().month() == 1 && dt.date().day() == 1 && dt.time().hour() == 0 && dt.time().minute() == 0 && dt.time().second() == 0) {
			fi.modified = QDateTime();
		}
	}

	return fi;
}

FileInfo2 WindowsFileSystemProvider::firstFileInfo() const
{
	// return WindowsFileSystemProvider::getFileInfo(m->hImageList, m->fileitem.idlist());
	return WindowsFileSystemProvider::getFileInfo(m->hImageList, FileItem::getDesktop().idlist());
}

bool WindowsFileSystemProvider::hasReadPermission() const
{
	if (m->fileitem) {
		HRESULT hr;
		IEnumIDList *enumidlist = nullptr;
		hr = m->fileitem.shfolder()->EnumObjects(nullptr, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS, &enumidlist);
		if (SUCCEEDED(hr)) {
			enumidlist->Release();
			return true;
		}
	}
	return false;
}

QIcon WindowsFileSystemProvider::icon()
{
	auto iidl = m->fileinfo.iidl;
	if (iidl.empty()) return QIcon();
	return QIcon(QPixmap::fromImage(getIcon(m->hImageList, iidl.d.iidl.data())));
}

