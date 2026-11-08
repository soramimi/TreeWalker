#include "WindowsShellAPI.h"
#include "WindowsCom.h"

#include <QDebug>
#include <QCoreApplication>
#include <QList>
#include <QThread>
#include <memory>
#include <shlobj.h>
#include <stdint.h>
#include <vector>
#include <string.h>

QList<QByteArray> parseITEMIDLIST(ITEMIDLIST const *iidl)
{
	QList<QByteArray> res;
	if (iidl) {
		char const *ptr = (char const *)iidl;
		while (1) {
			uint16_t n;
			memcpy(&n, ptr, 2);
			if (n < 3) {
				break;
			}
			QByteArray ba(ptr + 2, n - 2);
			res.push_back(ba);
			ptr += n;
		}
	}
	return res;
}

size_t iidlsize(ITEMIDLIST const *p)
{
	if (!p) return 0;
	char const *left = (char const *)p;
	char const *right = left;
	while (1) {
		uint16_t n;
		memcpy(&n, right, 2);
		if (n == 0) {
			right += 2;
			break;
		}
		right += n;
	}
	return right - left;
}

ITEMIDLIST *createITEMIDLIST(std::vector<char> const *vec, IMalloc *pMalloc)
{
	void *p = pMalloc->Alloc(vec->size());;
	if (p) {
		memcpy(p, &vec->at(0), vec->size());
	}
	return (ITEMIDLIST *)p;
}

void iidlcat(std::vector<char> *out, std::vector<char> const *left, std::vector<char> const *right)
{
	if (out == right) {
		std::vector<char> t;
		iidlcat(&t, left, right);
		std::swap(t, *out);
		return;
	}
	size_t n = left->size();
	if (n >= 2 && left->at(n - 1) == 0 && left->at(n - 2) == 0) {
		n -= 2;
		if (out == left) {
			out->resize(n);
		} else {
			out->clear();
			out->reserve(n + right->size());
			char const *begin = &left->at(0);
			char const *end = begin + n;
			out->insert(out->end(), begin, end);
		}
	} else {
		out->clear();
	}
	out->insert(out->end(), right->begin(), right->end());
}

bool iidlpop(std::vector<char> *iidl, std::vector<char> *last)
{
	last->clear();
	std::vector<int> arr;
	char const *begin = (char const *)&iidl->at(0);
	char const *ptr = begin;
	while (1) {
		arr.push_back(ptr - begin);
		uint16_t n;
		memcpy(&n, ptr, 2);
		if (n == 0) {
			ptr += 2;
			break;
		}
		ptr += n;
	}
	size_t i = arr.size();
	if (i < 2) return false;
	i -= 2;
	char const *p = begin + arr[i];
	size_t n;
	n = arr[i + 1] - arr[i];
	last->insert(last->end(), p, p + n);
	last->push_back(0);
	last->push_back(0);
	n = arr[i];
	iidl->at(n) = 0;
	iidl->at(n + 1) = 0;
	iidl->resize(n + 2);
	return true;
}

// WindowsShellAPI

static DWORD currentComThreadingModel()
{
	APTTYPE type;
	APTTYPEQUALIFIER qualifier;
	if (SUCCEEDED(CoGetApartmentType(&type, &qualifier))) {
		// Another thread's MTA can be inherited by a thread which has never
		// initialized COM. Such a thread still uses our Shell default of STA.
		if ((type == APTTYPE_MTA && qualifier == APTTYPEQUALIFIER_NONE)
			|| (type == APTTYPE_NA && qualifier == APTTYPEQUALIFIER_NA_ON_MTA)) {
			return COINIT_MULTITHREADED | COINIT_DISABLE_OLE1DDE;
		}
	}
	return COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE;
}

struct WindowsShellAPI::Private {
	WindowsComApartment apartment{currentComThreadingModel()};
	IMalloc *pMalloc = nullptr;
	IShellFolder *pDesktopFolder = nullptr;
	~Private()
	{
		if (pDesktopFolder) pDesktopFolder->Release();
		if (pMalloc) pMalloc->Release();
	}
};

WindowsShellAPI::WindowsShellAPI()
	: m(new Private)
{
	if (!m->apartment.initialized()) {
		qWarning() << "Cannot initialize Shell COM apartment:" << Qt::hex << m->apartment.result();
		return;
	}
	HRESULT result = SHGetMalloc(&m->pMalloc);
	if (FAILED(result)) {
		qWarning() << "Cannot obtain Shell allocator:" << Qt::hex << result;
		return;
	}
	result = SHGetDesktopFolder(&m->pDesktopFolder);
	if (FAILED(result)) qWarning() << "Cannot obtain Desktop Shell folder:" << Qt::hex << result;
}

WindowsShellAPI::~WindowsShellAPI()
{
	delete m;
}

WindowsShellAPI &WindowsShellAPI::forCurrentThread()
{
	thread_local WindowsShellAPI shell;
	return shell;
}

bool WindowsShellAPI::isReady() const
{
	return m->apartment.initialized() && m->pDesktopFolder && m->pMalloc;
}

IMalloc *WindowsShellAPI::imalloc()
{
	return m->pMalloc;
}

QByteArray WindowsShellAPI::buildITEMIDLIST(QList<QByteArray> const &in)
{
	QByteArray ba;
	for (int i = 0; i < in.size(); i++) {
		uint16_t n = in.at(i).size() + 2;
		char const *p = in.at(i).data();
		ba.append((char)(n & 0xff));
		ba.append((char)((n >> 8) & 0xff));
		ba.append(p, n - 2);
	}
	ba.append((char)0);
	ba.append((char)0);
	return ba;
}

IShellFolder *WindowsShellAPI::getDesktopShellFolder()
{
	return isReady() ? m->pDesktopFolder : nullptr;
}

IShellFolder *WindowsShellAPI::getShellFolder(ITEMIDLIST const *iidl)
{
	if (isReady() && iidl) {
		IShellFolder *shfolder = nullptr;
		if (*(uint16_t const *)iidl == 0) {
			m->pDesktopFolder->AddRef();
			return m->pDesktopFolder;
		}
		HRESULT r = m->pDesktopFolder->BindToObject(iidl, 0, IID_IShellFolder, (void **)&shfolder);
		if (r == NOERROR && shfolder) {
			return shfolder;
		}
		if (shfolder) shfolder->Release();
	}
	return nullptr;
}

IShellFolder *WindowsShellAPI::getShellFolder(QByteArray const &iidl)
{
	if (iidl.isEmpty()) return nullptr;
	return getShellFolder((ITEMIDLIST *)iidl.data());
}

QByteArray WindowsShellAPI::parseDisplayName(QString path)
{
	if (!isReady() || path.isEmpty()) return {};
	path = path.replace('/', '\\');
	ITEMIDLIST *iidl = nullptr;
	ULONG eaten = 0;
	HRESULT r = m->pDesktopFolder->ParseDisplayName(0, 0, (wchar_t *)path.utf16(), &eaten, &iidl, 0);
	QByteArray result;
	if (SUCCEEDED(r) && iidl) result = WindowsShellAPI::getByteArrayFromITEMIDLIST(iidl);
	CoTaskMemFree(iidl);
	return result;
}

QByteArray WindowsShellAPI::getByteArrayFromITEMIDLIST(ITEMIDLIST const *iidl)
{
	if (!iidl) return QByteArray();
	QList<QByteArray> a = parseITEMIDLIST(iidl);
	return buildITEMIDLIST(a);
}

QByteArray WindowsShellAPI::makeShellIDListArray_(const QList<QByteArray> &list)
{
	QByteArray ba = buildITEMIDLIST(list);
	char const *begin = (char const *)ba.data();
	char const *end = begin + ba.size();
	std::vector<char> vec(begin, end);
	std::vector<char> last;
	iidlpop(&vec, &last);
	std::vector<char> res(sizeof(UINT) * 3);
	((UINT *)&res[0])[0] = 1;
	((UINT *)&res[0])[1] = res.size();
	res.insert(res.end(), vec.begin(), vec.end());
	((UINT *)&res[0])[2] = res.size();
	res.insert(res.end(), last.begin(), last.end());
	return QByteArray(&res[0], res.size());
}

QList<QByteArray> WindowsShellAPI::listFromPath(QString path)
{
	QByteArray idl = parseDisplayName(path);
	return idl.isEmpty() ? QList<QByteArray>() : parseITEMIDLIST(reinterpret_cast<ITEMIDLIST const *>(idl.constData()));
}

QByteArray WindowsShellAPI::parent(const QByteArray &idl)
{
	if (!idl.isEmpty()) {
		QList<QByteArray> list = parseITEMIDLIST((ITEMIDLIST const *)idl.data());
		if (list.size() > 1) {
			list.pop_back();
			return buildITEMIDLIST(list);
		}
	}
	return QByteArray();
}

QString WindowsShellAPI::pathFromList(const ITEMIDLIST *iidl) const
{
	return getPathFromIDList(iidl);
}

QString WindowsShellAPI::pathFromList(QList<QByteArray> const &list) const
{
	QByteArray ba = buildITEMIDLIST(list);
	return pathFromList((ITEMIDLIST *)ba.data());
}

void WindowsShellAPI::parseShellIDListArray(QByteArray const &ba, QStringList *out)
{
	if (ba.size() >= 4) {
		char const *p = ba.data();
		UINT *a = (UINT *)p;
		UINT n = a[0];
		QList<QByteArray> parent = parseITEMIDLIST((ITEMIDLIST *)(p + a[1]));
		for (UINT i = 0; i < n; i++) {
			QList<QByteArray> child = parseITEMIDLIST((ITEMIDLIST *)(p + a[i + 2]));
			QList<QByteArray> list = parent;
			list.append(child);
			QByteArray ba = buildITEMIDLIST(list);
			ITEMIDLIST *pidl = (ITEMIDLIST *)ba.data();
			QString path = pathFromList(pidl);
			out->push_back(path);
		}
	}
}

QByteArray WindowsShellAPI::makeShellIDListArray(const QString &path)
{
	WindowsShellAPI shell;
	QList<QByteArray> list = shell.listFromPath(path);
	QByteArray ba = makeShellIDListArray_(list);
	return ba;
}

QByteArray WindowsShellAPI::getSpecialFolderLocation(int csidl)
{
	if (!forCurrentThread().isReady()) return {};
	ITEMIDLIST *pIDL = 0;
	HRESULT r = SHGetSpecialFolderLocation(0, csidl, &pIDL);
	QByteArray result;
	if (SUCCEEDED(r) && pIDL) result = getByteArrayFromITEMIDLIST(pIDL);
	CoTaskMemFree(pIDL);
	return result;
}

QString WindowsShellAPI::getPathFromIDList(ITEMIDLIST const *iidl)
{
	if (!iidl || !forCurrentThread().isReady()) return {};
	wchar_t tmp[MAX_PATH]{};
	return SHGetPathFromIDListW(iidl, tmp) ? QString::fromWCharArray(tmp) : QString();
}

QString WindowsShellAPI::getPathFromIDList(ItemIdList const &iidl)
{
	if (iidl.type() == ItemIdList::Type::PATH) {
		return QString::fromUtf8(iidl.iidl());
	}
	if (iidl.type() == ItemIdList::Type::WIN_SHELL_ITEMIDLIST) {
		if (iidl.size() < 2) return {};
		return getPathFromIDList((ITEMIDLIST *)iidl.data());
	}
	return {};
}

QByteArray WindowsShellAPI::getKnownFolderIDList(KNOWNFOLDERID const &id)
{
	if (!isReady()) return {};
	ITEMIDLIST *il = nullptr;
	HRESULT result = SHGetKnownFolderIDList(id, 0, nullptr, &il);
	QByteArray idl;
	if (SUCCEEDED(result) && il) idl = getByteArrayFromITEMIDLIST(il);
	CoTaskMemFree(il);
	return idl;
}

QList<QByteArray> WindowsShellAPI::enumChildren(IShellFolder *shfolder, ITEMIDLIST const *iidl)
{
	QList<QByteArray> items;
	// Worker STAs must service Shell callbacks. The GUI already has an event
	// loop; pumping it here could reenter a tree rebuild or window destruction.
	bool const pump_messages = !QCoreApplication::instance()
		|| QThread::currentThread() != QCoreApplication::instance()->thread();
	if (isReady() && shfolder && iidl) {
		HRESULT r;
		IEnumIDList *pEnum = 0;
		r = shfolder->EnumObjects(0, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS | SHCONTF_INCLUDEHIDDEN, &pEnum);
		if (r == S_OK && pEnum) {
			ULONG n = 0;
			ITEMIDLIST *pIDL1 = 0;
			while (true) {
				if (pump_messages && !WindowsComApartment::pumpMessages()) break;
				r = pEnum->Next(1, &pIDL1, &n);
				if (r != S_OK || n != 1 || !pIDL1) {
					CoTaskMemFree(pIDL1);
					break;
				}
				ITEMIDLIST *pIDL2 = ILCombine(iidl, pIDL1);
				if (pIDL2) items.push_back(WindowsShellAPI::getByteArrayFromITEMIDLIST(pIDL2));
				CoTaskMemFree(pIDL1);
				CoTaskMemFree(pIDL2);
				pIDL1 = nullptr;
			}
		}
		if (pEnum) pEnum->Release();
	}
	return items;
}

QList<QByteArray> WindowsShellAPI::enumChildren(IShellFolder *shfolder, const QByteArray &iidl)
{
	if (iidl.size() < 2) return {};
	return enumChildren(shfolder, (ITEMIDLIST const *)iidl.data());
}

HIMAGELIST WindowsShellAPI::getImageList(QByteArray const &iidl)
{
	if (iidl.size() < 2 || !forCurrentThread().isReady()) return nullptr;
	SHFILEINFO info1{};
	return (HIMAGELIST)SHGetFileInfo((wchar_t const *)iidl.data(), 0, &info1, sizeof(SHFILEINFO), SHGFI_PIDL | SHGFI_SMALLICON | SHGFI_SYSICONINDEX | SHGFI_DISPLAYNAME | SHGFI_ATTRIBUTES);
}

/*
 * GetPath from LNK file directly(without COM).
 */
QString Win32ParseShellLink(QString const &path)
{
	auto ReadInt16 = [](uint8_t const *bytes, int off){
		return ((bytes[off + 1] << 8) | bytes[off]);
	};
	auto ReadInt32 = [](uint8_t const *bytes, int off){
		return ((bytes[off + 3] << 24) | (bytes[off + 2] << 16) | (bytes[off + 1] << 8) | bytes[off]);
	};

	struct Local {
		LPBYTE buff = nullptr;
		HANDLE hMap = nullptr;
		HANDLE hFile = INVALID_HANDLE_VALUE;

		~Local()
		{
			if (buff) UnmapViewOfFile(buff);
			if (hMap) CloseHandle(hMap);
			if (hFile != INVALID_HANDLE_VALUE) CloseHandle(hFile);
		}
	} L;

	// map the entire file into a byte buffer
	L.hFile = CreateFileW((wchar_t const *)path.utf16(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (L.hFile != INVALID_HANDLE_VALUE) {
		L.hMap = CreateFileMapping(L.hFile, nullptr, PAGE_READONLY, 0, 0, nullptr);
		if (L.hMap) {
			L.buff = (uint8_t *)MapViewOfFile(L.hMap, FILE_MAP_READ, 0, 0, 0);
			if (L.buff) {
				// check the magic number
				const int shell_offset = 0x4c;
				if (ReadInt32(L.buff, 0) == shell_offset) {
					const byte clsid[] = {0x01, 0x14, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46};
					if (memcmp(L.buff + 4, clsid, sizeof(clsid)) == 0) {
						// check the flags
						const int flags_offset = 0x14;
						const int has_shell_mask = 0x01;
						const int has_linkinfo_mask = 0x02;
						const int force_no_linkinfo_mask = 0x0100;
						int flags = ReadInt32(L.buff, flags_offset);
						if (flags & force_no_linkinfo_mask) {
							flags &= ~has_linkinfo_mask;
						}
						if (flags & (has_shell_mask | has_linkinfo_mask)) {

							// if the shell settings are present, skip them
							int shell_len = 0;
							if (flags & has_shell_mask) {
								// the plus 2 accounts for the length marker itself
								shell_len = ReadInt16(L.buff, shell_offset) + 2;

								// handle without LinkInfo(Advertise shortcut?)
								if (!(flags & has_linkinfo_mask)) {
									int size = 1000;
									char *link = (char *)alloca(size);
									bool result = SHGetPathFromIDListA((ITEMIDLIST const *)(L.buff + shell_offset + 2), link);
									return result ? link : QString();
								}
							}

							// get to the file settings
							int linkinfo_start = shell_offset + shell_len;
							const int file_location_info_flag_offset_offset = 0x08;
							const int basename_offset_offset = 0x10;
							const int networkVolumeTable_offset_offset = 0x14;
							const int finalname_offset_offset = 0x18;

							// get the local volume and local system values
							int basename_offset = ReadInt32(L.buff, linkinfo_start + basename_offset_offset) + linkinfo_start;
							int finalname_offset = ReadInt32(L.buff, linkinfo_start + finalname_offset_offset) + linkinfo_start;

							int file_location_info_flag = ReadInt32(L.buff, linkinfo_start + file_location_info_flag_offset_offset);
							const int is_local_mask = 0x01;
							const int is_remote_mask = 0x02;
							if (file_location_info_flag & (is_local_mask | is_remote_mask)) {
								int size = 1000;
								char *link = (char *)alloca(size);

								if (file_location_info_flag & is_local_mask) {	//XXX - should check first
									strncpy(link, (char *)(L.buff + basename_offset), size);
								} else {
									// get remote share name
									int networkVolumeTable_offset = ReadInt32(L.buff, linkinfo_start + networkVolumeTable_offset_offset) + linkinfo_start;
									int shareName_offset_offset = 0x08;
									int shareName_offset = ReadInt32(L.buff, networkVolumeTable_offset + shareName_offset_offset) + networkVolumeTable_offset;

									strncpy(link, (char *)(L.buff + shareName_offset), size);
									strncat(link, "\\", size);
								}

								strncat(link, (char *)(L.buff + finalname_offset), size);
								QString ret = link;
								return ret;
							}
						}
					}
				}
			}
		}
	}
	return QString();
}

//

QImage WindowsShellAPI::convertHBitmapToQImage(HBITMAP hBitmap)
{
	BITMAP bm{};
	if (!hBitmap || !GetObject(hBitmap, sizeof(BITMAP), &bm) || bm.bmWidth < 1 || bm.bmHeight < 1) return {};

	BITMAPINFO bi;
	ZeroMemory(&bi, sizeof(bi));

	bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth = bm.bmWidth;
	bi.bmiHeader.biHeight = -bm.bmHeight; // top-down
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;

	QImage image(
		bm.bmWidth,
		bm.bmHeight,
		QImage::Format_ARGB32);

	HDC hdc = GetDC(NULL);
	if (!hdc) return {};

	int lines = GetDIBits(
		hdc,
		hBitmap,
		0,
		bm.bmHeight,
		image.bits(),
		&bi,
		DIB_RGB_COLORS);

	ReleaseDC(NULL, hdc);

	return lines == bm.bmHeight ? image : QImage();
}

QImage WindowsShellAPI::queryThumbnail(QString const &path)
{
	if (path.isEmpty() || !forCurrentThread().isReady()) return {};

	QString path2 = path;

#ifdef _WIN32
	path2.replace('/', '\\');
#endif

	QImage ret;

	IShellItemImageFactory* factory = nullptr;

	HRESULT hr = SHCreateItemFromParsingName(
				(wchar_t const *)path2.utf16(),
				nullptr,
				IID_PPV_ARGS(&factory));

	if (SUCCEEDED(hr) && factory) {
		HBITMAP hbm = nullptr;

		SIZE size = { 128, 128 };

		hr = factory->GetImage(
					size,
					SIIGBF_BIGGERSIZEOK,
					&hbm);

		if (SUCCEEDED(hr) && hbm) {
			ret = convertHBitmapToQImage(hbm);
		}
		if (hbm) DeleteObject(hbm);
	}
	if (factory) factory->Release();

	return ret;
}

QImage winQueryThumbnail(const QString &path)
{
	return WindowsShellAPI::queryThumbnail(path);
}


void WindowsShellAPI::test()
{
	WindowsComApartment apartment;
	if (!apartment.initialized()) return;

	QImage ret = queryThumbnail("Z:\\pictures\\camera\\EOS-R10\\2022-08-17\\IMG_0425.JPG");

	// qDebug() << ret.width() << ret.height();
}

