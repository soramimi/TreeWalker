#include "WindowsFileSystemProvider.h"
#include "WindowsShellAPI.h"
#include "common/str.h"
#include <QFileInfo>
#include <QHash>
#include <QPixmap>
#include <commoncontrols.h>
#include <shlwapi.h>

static FileInfo2 getNetworkFileInfo(HIMAGELIST_ hImageList, ItemIdList const &iidl, QHash<int, QImage> &icon_cache);

static inline WindowsShellAPI *shapi()
{
	return &WindowsShellAPI::forCurrentThread();
}

bool WindowsFileSystemProvider::isPhysicalFilesystemFolder(void const *iidl)
{
	if (!iidl || !shapi()->isReady()) return false;
	IShellItem *item = nullptr;

	HRESULT hr;

	hr = SHCreateItemFromIDList(
				(ITEMIDLIST const *)iidl,
				IID_PPV_ARGS(&item));

	if (FAILED(hr) || !item) {
		if (item) item->Release();
		return false;
	}

	SFGAOF attrs = 0;

	hr = item->GetAttributes(
				SFGAO_FOLDER | SFGAO_FILESYSTEM,
				&attrs);
	item->Release();

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
#ifdef Q_OS_WIN
	if (iidl[0] == '/' && iidl[1] == '/') { // network drive
		return true;
	}
#endif
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
	if (!shapi()->isReady()) return {};
	Data *p = new Data;
	p->idlist = WindowsShellAPI::getSpecialFolderLocation(CSIDL_DESKTOP);
	SHGetDesktopFolder(&p->shfolder);
	return FileItem(p);
}

FileItem FileItem::getDrives()
{
	if (!shapi()->isReady()) return {};
	Data *p = new Data;
	p->idlist = WindowsShellAPI::getSpecialFolderLocation(CSIDL_DRIVES);
	SHGetDesktopFolder(&p->shfolder);
	return FileItem(p);
}

FileItem FileItem::getNetwork()
{
	if (!shapi()->isReady()) return {};
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
		if (idl.d.type == ItemIdList::Type::PATH) {
			QString path = idl.path();
			if (path.startsWith("//") && path.indexOf("//", 2) > 2) {
				if (path.startsWith((misc::str)prefix_itemidlist)) {
					QByteArray ba = QByteArray::fromHex(path.mid(prefix_itemidlist.size()).toLatin1());
					p->idlist = ItemIdList(ba);
					p->shfolder = shapi()->getShellFolder(p->idlist.d.iidl);
					return {p};
				} else if (path.startsWith(QLatin1StringView(prefix_mycomputer))) {
				}
				return {p};
			}
			p->idlist = ItemIdList(path);
			return {p};
		}
		p->idlist = WindowsShellAPI::getByteArrayFromITEMIDLIST((ITEMIDLIST const *)idl.data());
		p->shfolder = shapi()->getShellFolder(p->idlist.d.iidl);
	}
	return {p};
}

// WindowsFileSystemProvider

struct WindowsFileSystemProvider::Private {
	QString dir;
	QList<ItemIdList> children;
	int children_pos = -1;
	FileItem fileitem;
	FileInfo2 fileinfo;
	HIMAGELIST hImageList = nullptr;
	bool is_network = false;
	QHash<int, QImage> network_icon_cache; // system image index -> worker-safe image
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
	m->is_network = (m->fileitem.idlist() == FileItem::getNetwork().idlist());
	updateImageList();
}

WindowsFileSystemProvider::~WindowsFileSystemProvider()
{
	delete m;
}

ItemIdList WindowsFileSystemProvider::iidl() const
{
	return m->fileitem.idlist();
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
			QList<QByteArray> list = shapi()->enumChildren(m->fileitem.shfolder(), m->fileitem.idlist().d.iidl);
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
		if (m->is_network) {
			m->fileinfo = getNetworkFileInfo(m->hImageList, m->children[m->children_pos], m->network_icon_cache);
		} else {
			m->fileinfo = getFileInfo(m->hImageList, m->children[m->children_pos]);
		}
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
	if (!hIcon) return {};
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
	ICONINFO info{};
	QImage image;
	if (GetIconInfo(hIcon, &info)) {
		if (info.hbmColor) image = WindowsShellAPI::convertHBitmapToQImage(info.hbmColor);
		if (info.hbmColor) DeleteObject(info.hbmColor);
		if (info.hbmMask) DeleteObject(info.hbmMask);
	}
	return image;
#else
	return QImage::fromHICON(hIcon);
#endif
}

QImage WindowsFileSystemProvider::getIcon(HIMAGELIST_ hImageList, void const *iidl)
{
	if (!iidl || !shapi()->isReady()) return {};
	SHFILEINFO sfinfo{};
	// UINT flags = SHGFI_PIDL | SHGFI_SMALLICON | SHGFI_SYSICONINDEX | SHGFI_DISPLAYNAME | SHGFI_ATTRIBUTES;
	UINT flags = SHGFI_PIDL | SHGSI_ICON | SHGSI_LARGEICON | SHGFI_SYSICONINDEX;
	HIMAGELIST images = reinterpret_cast<HIMAGELIST>(SHGetFileInfo((wchar_t const *)iidl, 0, &sfinfo, sizeof(SHFILEINFO), flags));
	if (!hImageList) hImageList = images;
	if (!images || !hImageList) return {};
	HICON hIcon = ImageList_GetIcon(reinterpret_cast<HIMAGELIST>(hImageList), sfinfo.iIcon, ILD_NORMAL);
	QImage im = convertHIconToQImage(hIcon);
	DestroyIcon(hIcon);
	return im;
}

QImage WindowsFileSystemProvider::getStockFolderIcon()
{
	if (!shapi()->isReady()) return {};
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

	static thread_local QImage static_image;

	if (static_image.isNull()) {
		HRESULT hr;

		SHSTOCKICONINFO sii = {};
		sii.cbSize = sizeof(sii);

		hr = SHGetStockIconInfo(
			SIID_FOLDER,
			SHGSI_SYSICONINDEX,
			&sii);
		if (FAILED(hr)) return {};

		IImageList *imageList = nullptr;

		hr = SHGetImageList(
			SHIL_JUMBO,
			IID_PPV_ARGS(&imageList));
		if (FAILED(hr) || !imageList) {
			if (imageList) imageList->Release();
			return {};
		}

		HICON hIcon = nullptr;

		hr = imageList->GetIcon(
			sii.iSysImageIndex,
			ILD_TRANSPARENT,
			&hIcon);
		imageList->Release();
		if (SUCCEEDED(hr) && hIcon) static_image = convertHIconToQImage(hIcon);
		if (hIcon) DestroyIcon(hIcon);
	}

	ret = static_image;
#endif
	return ret;
}

FileInfo2 WindowsFileSystemProvider::getFileInfo(HIMAGELIST_ hImageList, ItemIdList const &iidl)
{
	Q_ASSERT(iidl.type() == ItemIdList::Type::WIN_SHELL_ITEMIDLIST);
	if (iidl.d.iidl.size() < 2 || !shapi()->isReady()) return {};

	FileInfo2 fi;

	SHFILEINFO sfinfo{};
	ITEMIDLIST const *iidl_p = reinterpret_cast<ITEMIDLIST const *>(iidl.data());

	if (!SHGetFileInfo((wchar_t const *)iidl_p, 0, &sfinfo, sizeof(SHFILEINFO), SHGFI_PIDL | SHGFI_DISPLAYNAME | SHGFI_ATTRIBUTES) || sfinfo.dwAttributes == 0) {
		return FileInfo2();
	}

	fi.path = WindowsShellAPI::getPathFromIDList(iidl_p);
	if (fi.path.isEmpty() && !(sfinfo.dwAttributes & SFGAO_FOLDER)) {
		return FileInfo2();
	}

	QFileInfo qfi(fi.path);
	fi.iidl = iidl.d.iidl;
	fi.name = QString::fromUtf16((ushort const *)sfinfo.szDisplayName);
	fi.isdir = fi.path.isEmpty() || qfi.isDir() || (sfinfo.dwAttributes & SFGAO_FOLDER);

	if (isPhysicalFilesystemFolder(iidl_p)) {
		// 物理ファイルシステムのフォルダはストックアイコンを使用する。
		// （プレビューつきフォルダアイコンを生成させないため）
		fi.image = getStockFolderIcon();
	} else {
		// 通常ファイルと仮想フォルダはシェルAPIからアイコンを取得する。
		fi.image = getIcon(hImageList, iidl_p);
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

// ネットワーク直下の項目用の軽量版。
// QFileInfo（UNCパスへのアクセス）や項目ごとのアイコン生成を避ける。
static FileInfo2 getNetworkFileInfo(HIMAGELIST_ hImageList, ItemIdList const &iidl, QHash<int, QImage> &icon_cache)
{
	if (iidl.size() < 2 || !shapi()->isReady()) return {};

	ITEMIDLIST const *iidl_p = reinterpret_cast<ITEMIDLIST const *>(iidl.data());

	SHFILEINFO sfinfo{};
	SHGetFileInfo((wchar_t const *)iidl_p, 0, &sfinfo, sizeof(SHFILEINFO), SHGFI_PIDL | SHGFI_DISPLAYNAME | SHGFI_ATTRIBUTES | SHGFI_SYSICONINDEX);
	if (sfinfo.dwAttributes == 0) {
		return {};
	}

	FileInfo2 fi;
	fi.path = WindowsShellAPI::getPathFromIDList(iidl_p);
	if (fi.path.isEmpty() && !(sfinfo.dwAttributes & SFGAO_FOLDER)) {
		return {};
	}

	fi.iidl = iidl;
	fi.name = QString::fromUtf16((ushort const *)sfinfo.szDisplayName);
	fi.isdir = fi.path.isEmpty() || (sfinfo.dwAttributes & SFGAO_FOLDER);

	auto it = icon_cache.find(sfinfo.iIcon);
	if (it == icon_cache.end()) {
		QImage icon;
		HICON hIcon = ImageList_GetIcon(reinterpret_cast<HIMAGELIST>(hImageList), sfinfo.iIcon, ILD_NORMAL);
		if (hIcon) {
			icon = convertHIconToQImage(hIcon);
			DestroyIcon(hIcon);
		}
		it = icon_cache.insert(sfinfo.iIcon, icon);
	}
	fi.image = it.value();

	return fi;
}

bool WindowsFileSystemProvider::hasReadPermission() const
{
	if (!m->fileitem) return false;
	IShellFolder *folder = m->fileitem.shfolder();
	if (!folder) return false;

	IEnumIDList *enumidlist = nullptr;
	HRESULT const hr = folder->EnumObjects(nullptr, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS, &enumidlist);
	// An empty Shell folder may return S_FALSE with no enumerator.
	bool const readable = hr == S_FALSE || (hr == S_OK && enumidlist);
	if (enumidlist) {
		enumidlist->Release();
	}
	return readable;
}

QIcon WindowsFileSystemProvider::icon()
{
	auto iidl = m->fileinfo.iidl;
	if (iidl.empty()) return QIcon();
	return QIcon(QPixmap::fromImage(getIcon(m->hImageList, iidl.d.iidl.data())));
}

