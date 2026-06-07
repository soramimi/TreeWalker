#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QFileInfo>
#include <QMainWindow>
#include <functional>
#include "AbstractFileSystemProvider.h"
#include "FileItemModel.h"
#include "FolderTreeView.h"
#include "ThumbnailLoader.h"

namespace Ui {
class MainWindow;
}

class ApplicationSettings;

class QFileInfo;
class QTreeWidget;
class QTableWidgetItem;
class FolderTreeView;

#ifdef Q_OS_WIN
struct IShellFolder;
#endif

using add_tree_child_fn_r = std::function<void(FolderTreeItem *parent, FolderTreeItem *item)>;

struct LocationData {
	Kind kind;
	ItemIdList iidl;
	QString path;
	QList<FileInfo2> files;
	bool isDir() const
	{
		return kind == Kind::Directory || kind == Kind::SubDirectory || kind == Kind::NotPermittedDirectory;
	}
	bool isPermitted() const
	{
		return kind != Kind::NotPermittedDirectory;
	}
	bool operator == (LocationData const &r) const
	{
		return kind == r.kind && iidl == r.iidl && path == r.path;
	}
	bool operator != (LocationData const &r) const
	{
		return !operator == (r);
	}
};
Q_DECLARE_METATYPE(LocationData)

class MainWindow : public QMainWindow {
	Q_OBJECT
	friend class MyApplication;
	friend class FetchLocationThread;
private:
	struct Private;
	Private *m;
	public:
	explicit MainWindow(QWidget *parent = 0);
	~MainWindow();

	enum ViewMode {
		List,
		Thumbnail,
	};

	ViewMode viewmode() const;
	void setViewMode(ViewMode mode);

	QString currentLocation();
	QString currentFilePath();
	void setAddressBarVisible(bool visible);
	void toggleBookbarkBarVisible();
public:
	void reloadContents();
	static void sortFileInfoList(QList<FileInfo2> *list);
	void setStatusBarText(const QString &text);
	QList<FileInfo2> windowsMyComputerFiles();
	QImage queryThumbnail(const QString &path);
	void setFocusFolderTree();
	FileItemModel *fileitemmodel();
	QIcon getIcon(const FileInfo2 &info);
private:
	void clearIconCache();
private slots:
	void onHotKey(size_t id);
	void onRefreshFileListDone(const LocationData &loc);
	void onRename();
	void on_action_edit_sttings_triggered();
	void on_action_test_triggered();
	void on_action_view_detailed_triggered();
	void on_action_view_thumbnails_triggered();
	void on_tableView_customContextMenuRequested(const QPoint &pos);
	void on_treeView_currentItemChanged(FolderTreeItem *current, FolderTreeItem *previous);
private:
	void updateFileView();
	void updateViews();
private:
	Ui::MainWindow *ui;
	QString typeText(const FileInfo2 &info);
	QString modifiedText(const FileInfo2 &info);
	QString sizeText(const FileInfo2 &info);
	QString nameText(const FileInfo2 &info);

	struct TreeInfo {
#ifdef Q_OS_WIN
		ItemIdList iidl;
		ItemIdList nextiidl;
#else
		QString path;
		QString nextpath;
#endif
		FolderTreeItem *treeitem;
	};
	void makeTree(AbstractFileSystemProvider *fs, FolderTreeItem *parent, TreeInfo *find = nullptr);
	void fetchSubFolders(FolderTreeItem *parent = {});

	FolderTreeItem *findBookmarkTreeItem(QString const &path, FolderTreeItem *item);
#ifdef Q_OS_WIN
	FolderTreeItem *openDirWindows(const ItemIdList &iidl);
#else
	FolderTreeItem *openDirPosix(const ItemIdList &iidl);
#endif
	bool hiddenFilesVisible() const;
	void fetchBookmarks();
	void makeBookmarkMap(const QString &path, QJsonValue v, int depth, FolderTreeItem *parent);
	void openDir(const ItemIdList &iidl);
	void openTableItem(const QModelIndex &index);
	void refreshFileList2(const LocationData &loc, bool force);
	void renameFile(const QString &path);
	void renameFolder(const QString &location);
	void setAppSettings(const ApplicationSettings &appsettings);
	void setHiddenFilesVisible(bool f);
	void setTreeViewSubDirItemData(FolderTreeItem *rootitem, const FileInfo2 &info);
	void moveToParent();
	QString currentPath() const;
	ItemIdList currentIIDL() const;
	void setCurrentDir(const QString &dir);
	FileSystemProviderPtr newFileSystemPtr(const ItemIdList &iidl);
	void toggleTreeItemExpansion();
	FolderTreeItem *makeTreeCompletely();
	void setCurrentIIDL(ItemIdList iidl);
	bool hasSubDir(AbstractFileSystemProvider *fs, const ItemIdList &iidl);
protected:
	bool acceptKeyEvent(QKeyEvent *event);
	void keyPressEvent(QKeyEvent *);
private slots:
	void thumbnailReady(std::shared_ptr<ThumbnailLoader::Entiry> entity);
	void on_action_view_hide_item_triggered();
	void on_treeView_itemExpanded(FolderTreeItem *item);
};

class VirtualFileSystem {
public:
};

#endif // MAINWINDOW_H
