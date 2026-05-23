#ifndef WINDOWSSHELLAPI_H
#define WINDOWSSHELLAPI_H

#include "ItemIdList.h"

#include <QString>
#include <QList>
#include <QByteArray>
#include <QImage>

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>

class WindowsShellAPI {
public:
private:
	struct Private;
	Private *m;
	class ItemList {
	public:
		QList<QByteArray> list;
	};
	static QByteArray makeShellIDListArray_(const QList<QByteArray> &list);
	QList<QByteArray> listFromPath(QString path);
	bool GetContextMenu(void **ppContextMenu, int &iMenuType);
public:
	WindowsShellAPI();
	~WindowsShellAPI();
	IMalloc *imalloc();
	static QByteArray getByteArrayFromITEMIDLIST(const ITEMIDLIST *iidl);
	static QByteArray parent(QByteArray const &idl);
	QString pathFromList(ITEMIDLIST const *iidl) const;
	QString pathFromList(const QList<QByteArray> &list) const;
	void parseShellIDListArray(const QByteArray &ba, QStringList *out);
	static QByteArray makeShellIDListArray(const QString &path);
	static QByteArray buildITEMIDLIST(const QList<QByteArray> &in);
	static QByteArray buildITEMIDLIST(ITEMIDLIST const *in);
	IShellFolder *getDesktopShellFolder();
	IShellFolder *getShellFolder(ITEMIDLIST const *iidl);
	IShellFolder *getShellFolder(QByteArray const &iidl);
	QByteArray parseDisplayName(QString path);
	static QByteArray getSpecialFolderLocation(int csidl);
	static QString getPathFromIDList(ITEMIDLIST const *iidl);
	static QString getPathFromIDList(const ItemIdList &iidl);
	QByteArray getKnownFolderIDList(const KNOWNFOLDERID &id);
	QList<QByteArray> enumChildren(IShellFolder *shfolder, const ITEMIDLIST *iidl);
	QList<QByteArray> enumChildren(IShellFolder *shfolder, const QByteArray &iidl);
	static HIMAGELIST getImageList(QByteArray const &iidl);
	static QImage convertHBitmapToQImage(HBITMAP hBitmap);
	static QImage queryThumbnail(const QString &path);
	static void test();
};


QImage winQueryThumbnail(const QString &path);


//QByteArray makeShellIDListArray(QString const &path);

QString Win32ParseShellLink(const QString &path);


#endif // WINDOWSSHELLAPI_H
