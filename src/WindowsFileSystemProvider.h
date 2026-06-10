#ifndef WINDOWSFILESYSTEMPROVIDER_H
#define WINDOWSFILESYSTEMPROVIDER_H

#include "AbstractFileSystemProvider.h"

class WindowsShellAPI;
struct IShellFolder;
struct KNOWNFOLDERID_;
struct ITEMIDLIST_;
typedef void *HIMAGELIST_;

class FileItem {
	friend class WindowsFileSystemProvider;
private:
	struct Data;
	std::shared_ptr<Data> dataptr;
	FileItem(Data *p);
	IShellFolder *shfolder() const;
public:
	FileItem();
	operator bool () const;
	ItemIdList idlist() const;
	static FileItem getDesktop();
	static FileItem getDrives();
	static FileItem getNetwork();
	static FileItem getKnownFolder(KNOWNFOLDERID_ const &id);
	static FileItem parseFolder(QString const &path);
	static FileItem fromITEMIDLIST(ITEMIDLIST_ const *idl);
	static FileItem fromITEMIDLIST(const ItemIdList &idl);
};

class WindowsFileSystemProvider : public AbstractFileSystemProvider {
private:
	struct Private;
	Private *m;
	void setDir(QString const &d);
	void updateImageList();
public:
	WindowsFileSystemProvider(QString const &dir = QString());
	WindowsFileSystemProvider(const ItemIdList &iidl);
	~WindowsFileSystemProvider();
	ItemIdList iidl() const;
	QString currentDir() const;
	bool fetch();
	QString fileName() const;
	FileInfo2 fileInfo() const;
	bool isDir() const;
	static QImage getIcon(HIMAGELIST_ hImageList, void const *iidl);
	static FileInfo2 getFileInfo(HIMAGELIST_ hImageList, ItemIdList const &iidl);
	FileInfo2 drivesFileInfo() const;
	bool hasReadPermission() const;
	QIcon icon();

	FileSystemProviderPtr create(ItemIdList const &iidl)
	{
		return FileSystemProviderPtr(new WindowsFileSystemProvider(iidl));
	}

	virtual std::shared_ptr<AbstractFileSystemProvider> dup(ItemIdList const &iidl)
	{
		std::shared_ptr<WindowsFileSystemProvider> ret = std::make_shared<WindowsFileSystemProvider>(iidl);
		return ret;
	}

	static bool isPhysicalFilesystemFolder(void const *iidl);
	static bool isPhysicalFilesystemFolder(const ItemIdList &iidl);
	static QImage getStockFolderIcon();
};

#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
#include <QtWinExtras>
#endif


#endif // WINDOWSFILESYSTEMPROVIDER_H
