
#include "MainWindow.h"
#include "ui_MainWindow.h"
#include "ApplicationGlobal.h"
#include "BasicFileSystemProvider.h"
#include "FetchLocationThread.h"
#include "RenameDialog.h"
#include "SettingsDialog.h"
#include "StatusLabel.h"
#include "joinpath.h"
#include <QDateTime>
#include <QDesktopServices>
#include <QElapsedTimer>
#include <QFileIconProvider>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QStandardPaths>
#include <QTableWidgetItem>
#include <QTimer>
#include "realpath.h"

#ifdef Q_OS_WIN
#include "NetworkDiscoveryThread.h"
#include "WindowsFileSystemProvider.h"
#include "WindowsShellAPI.h"
#endif

QString dumpByteArray(QByteArray const &ba)
{
	QString text;
	for (int i = 0; i < ba.size(); i++) {
		char tmp[3];
		sprintf(tmp, "%02x", ba[i]);
		text += tmp;
	}
	return text;
}


static inline FolderTreeItem *new_FolderTreeItem()
{
	FolderTreeItem *item = new FolderTreeItem();
	return item;
}

static inline QTableWidgetItem *new_QTableWidgetItem(QString const &text)
{
	QTableWidgetItem *item = new QTableWidgetItem;
	item->setSizeHint(QSize(20, 20));
	item->setText(text);
	return item;
}

class BookmarkInfo {
public:
	QString name;
	QString url;
	BookmarkInfo(QString const &name = QString(), QString const &url = QString())
		: name(name)
		, url(url)
	{
	}
};

struct MainWindow::Private {
	FileItemModel file_item_model;

	ItemIdList current_iidl;
	ThumbnailLoader thumbnail_loader;
	StatusLabel *status_label;
	QTimer update_list_timer;
	FileSystemProviderPtr fs_factory;
	FolderTreeItem *my_computer_item;
	FolderTreeItem *drive_root;
	FolderTreeItem *root_dir_item; // for POSIX=/, for Windows=Desktop

	FolderTreeItem *bookmarks_root_item;

	QFileIconProvider icon_provider;
	QIcon default_file_icon;
	QIcon default_folder_icon;

	std::map<QString, QList<BookmarkInfo>> bookmark_items;

	LocationData current_location;
	QString last_location;
	QList<std::shared_ptr<FetchLocationThread>> fetch_location_threads;

	bool hidden_files_visible = false;

	std::map<QString, QIcon> icon_cache;

	std::set<ItemIdList> iidl_hide_set;
#ifdef Q_OS_WIN
	NetworkDiscoveryThread network_discovery_thread;
#endif
};

static QString getDisplayName(FileInfo2 const &info)
{
	return info.name;
}

void MainWindow::addPlaceholder(FolderTreeItem *item)
{
	auto placeholder = new_FolderTreeItem();
	placeholder->setData(0, KindRole, (int)Kind::Placeholder);
	ui->treeView->setExpanded(item, false);
	item->addChild(placeholder);
}

MainWindow::MainWindow(QWidget *parent)
	: QMainWindow(parent)
	, m(new Private())
	, ui(new Ui::MainWindow)
{
	ui->setupUi(this);

	qApp->installEventFilter(this);
	
	m->status_label = new StatusLabel(this);
	ui->statusBar->addWidget(m->status_label);

	ui->tableView->verticalHeader()->setVisible(false);

	ui->frame_tool_bar->setVisible(false);

	m->default_file_icon = QFileIconProvider().icon(QFileIconProvider::File);
	m->default_folder_icon = QFileIconProvider().icon(QFileIconProvider::Folder);

	connect(&m->update_list_timer, &QTimer::timeout, [&](){
		updateFileView();
	});

	connect(ui->treeView, &FolderTreeView::currentItemChanged, [&](FolderTreeItem *current, FolderTreeItem *previous){
		on_treeView_currentItemChanged(current, previous);
	});

	connect(ui->treeView, &FolderTreeView::expanded, [&](FolderTreeItem *item){
		on_treeView_itemExpanded(item);
	});

	connect(ui->tableView, &FileTableView::itemDoubleClicked, [&](QModelIndex const &index){
		openTableItem(index);
	});

#ifdef Q_OS_WIN
	connect(&m->network_discovery_thread, &NetworkDiscoveryThread::filesChanged, [&](){
		auto item = ui->treeView->itemFromIidl(FileItem::getNetwork().idlist());
		if (item) {
			addPlaceholder(item);
			if (item == ui->treeView->currentItem()) {
				fetchSubFolders(item);
			}
		}
	});
#endif


#ifdef Q_OS_WIN
	m->network_discovery_thread.start();
	m->fs_factory = FileSystemProviderPtr(new WindowsFileSystemProvider());
#else
	m->fs_factory = FileSystemProviderPtr(new BasicFileSystemProvider(QString("/").toUtf8()));
#endif
}

MainWindow::~MainWindow()
{
	for (std::shared_ptr<FetchLocationThread> &thread : m->fetch_location_threads) {
		if (thread->isRunning()) {
			thread->requestInterruption();
			thread->wait();
		}
	}
#ifdef Q_OS_WIN
	m->network_discovery_thread.detach();
#endif
	delete m;
	delete ui;
}

FileItemModel *MainWindow::fileitemmodel()
{
	return &m->file_item_model;
}

void MainWindow::reloadContents()
{
#ifdef Q_OS_WIN
	QString hidelist[] = { // 邪魔なので消すリスト
		"//iidl//14001f400e3174f8b7b6dc47bc84b9e6b38f59030000", // Home
		"//iidl//14001f41ea6588e81c0e204e9aa6edcd0212c87c0000", // Gallery
		"//iidl//14001f5425481e03947bc34db131e946b44c8dd50000", // Library
		"//iidl//14001f77d1a4b4b254274041a2eb9a76d9d7cdc60000", // Linux
	};
	for (size_t i = 0; i < std::size(hidelist); i++) {
		m->iidl_hide_set.emplace(hidelist[i]);
	}
#endif

	makeTreeCompletely();

	ui->tableView->setModel(fileitemmodel());
	ui->thumbnailView->setModel(fileitemmodel());
	
	connect(&m->thumbnail_loader, &ThumbnailLoader::taskDone, this, &MainWindow::thumbnailReady);
	m->thumbnail_loader.start();
	
	ui->stackedWidget_fileview->setCurrentWidget(ui->page_list);
	
	fetchBookmarks();

	ui->treeView->setFocus();
	ui->treeView->setExpanded(m->drive_root, true);
	ui->treeView->setCurrentItem(m->drive_root);
}

MainWindow::ViewMode MainWindow::viewmode() const
{
	if (ui->stackedWidget_fileview->currentWidget() == ui->page_list) {
		return List;
	} else {
		return Thumbnail;
	}
}

void MainWindow::setViewMode(ViewMode mode)
{
	switch (mode) {
	case List:
		ui->stackedWidget_fileview->setCurrentWidget(ui->page_list);
		break;
	case Thumbnail:
		ui->stackedWidget_fileview->setCurrentWidget(ui->page_thumbnail);
		break;
	}
}

void MainWindow::onHotKey(size_t id)
{
	show();
}

void MainWindow::setStatusBarText(QString const &text)
{
	m->status_label->setText(text);
}

void MainWindow::setTreeViewSubDirItemData(FolderTreeItem *item, FileInfo2 const &info)
{
	QString path = info.path;
	ItemIdList iidl = info.iidl;
#ifdef Q_OS_WIN
	path.replace('\\', '/');
	if (iidl.size() >= 2 && iidl[0] == '/' && iidl[1] == '/') {
		QString path2 = iidl.path();
		iidl = global->shapi->parseDisplayName(path2);
		iidl = global->shapi->parseDisplayName(path2);
	}
#endif
	item->setText(0, getDisplayName(info));
	item->setData(0, KindRole, (int)Kind::SubDirectory);
	item->setData(0, NameRole, info.name);
	item->setData(0, PathRole, path);
	item->setData(0, IidlRole, QVariant::fromValue<ItemIdList>(iidl));
}

bool MainWindow::hasSubDir(AbstractFileSystemProvider *fs, ItemIdList const &iidl)
{
	auto fs2 = fs->dup(iidl);
	while (fs2->fetch()) {
		FileInfo2 info2 = fs2->fileInfo();
		if (info2.isdir) {
			return true;
		}
	}
	return false;
}

void MainWindow::makeTree(AbstractFileSystemProvider *fs, FolderTreeItem *parent, TreeInfo *find)
{
	clearIconCache();

	std::vector<FileInfo2> dirs;

#ifdef Q_OS_WIN
	if (WindowsFileSystemProvider *wfs = dynamic_cast<WindowsFileSystemProvider *>(fs)) {
		if (wfs->iidl() == FileItem::getNetwork().idlist()) {
			std::vector<FileInfo2> dirs2 = m->network_discovery_thread.files();
			for (FileInfo2 const &info : dirs2) {
				if (info.isdir) {
					if (info.name == "." || info.name == "..") {
						continue;
					}
					dirs.push_back(info);;
				}
			}
			goto happy;
		}
	}
#endif

	while (fs->fetch()) {
		FileInfo2 info = fs->fileInfo();
		if (info.isdir) {
			QString name = info.name;
			if (name == "." || name == "..") {
				continue;
			}
			if (!m->hidden_files_visible && name.startsWith('.')) {
				continue;
			}
			dirs.push_back(info);
		}
	}

happy:;

	{ // remove placeholder
		int i = parent->childCount();
		while (i > 0) {
			i--;
			delete parent->takeChild(i);
		}
	}

	if (dirs.empty()) {
		// addPlaceholder(parent);
	} else {
		sortFileInfoList(&dirs);

		for (FileInfo2 const &info: dirs) {
			if (m->iidl_hide_set.find(info.iidl) != m->iidl_hide_set.end()) {
				continue;
			}
			auto item = new_FolderTreeItem();
			setTreeViewSubDirItemData(item, info);
#ifdef Q_OS_WIN
			const bool check_subdir = false;
#else
			const bool check_subdir = true;
#endif
			if (check_subdir) {
				// サブディレクトリがあるものだけplaceholderを追加する版
				// Windowsでは遅すぎて使い物にならない
				if (hasSubDir(fs, info.iidl)) {
					addPlaceholder(item);
				}
			} else {
				// 全部の子にplaceholderを追加する版
				addPlaceholder(item);
			}
			parent->addChild(item);
			if (find) {
#ifdef Q_OS_WIN
				int len = info.iidl.size();
				if (len > 4) {
					len -= 4;
					if (find->iidl.size() > len) {
						if (memcmp(info.iidl.data(), find->iidl.data(), len) == 0) {
							find->nextiidl = info.iidl;
							find->treeitem = item;
						}
					}
				}
#else
				if (find->path.startsWith(info.path)) {
					uchar c = find->path.utf16()[info.path.size()];
					if (c == '/' || c == 0) {
						find->nextpath = info.path;
						find->treeitem = item;
					}
				}
#endif
			}
		}

		ui->treeView->setExpanded(parent, true);
	}
}

FolderTreeItem *MainWindow::makeTreeCompletely()
{
	ui->treeView->clear();

	FileInfo2 info = firstFileInfo();
	auto fs = m->fs_factory->create(info.iidl);
	
	m->root_dir_item = new_FolderTreeItem();
	setTreeViewSubDirItemData(m->root_dir_item, info);

	m->my_computer_item = new_FolderTreeItem();
	m->my_computer_item->setText(0, tr("My Computer"));
	m->my_computer_item->setData(0, KindRole, (int)Kind::Directory);
	ui->treeView->addTopLevelItem(m->my_computer_item);
	m->my_computer_item->addChild(m->root_dir_item);
	m->my_computer_item->setData(0, IidlRole, QVariant::fromValue<ItemIdList>(prefix_mycomputer));

	makeTree(fs.get(), m->root_dir_item);
	m->drive_root = m->root_dir_item;

	{
		FileInfo2 info2 = desktopFileInfo();
		auto *item = new_FolderTreeItem();
		setTreeViewSubDirItemData(item, info2);
		addPlaceholder(item);
		m->my_computer_item->addChild(item);
	}

#ifdef Q_OS_WIN
	{
		FileInfo2 info3 = networkFileInfo();
		auto *item = new_FolderTreeItem();
		setTreeViewSubDirItemData(item, info3);
		addPlaceholder(item);
		m->my_computer_item->addChild(item);
	}
#endif

	m->bookmarks_root_item = new_FolderTreeItem();
	m->bookmarks_root_item->setText(0, tr("Bookmarks"));
	m->bookmarks_root_item->setData(0, KindRole, (int)Kind::ChromeBookmark);
	m->bookmarks_root_item->setData(0, PathRole, prefix_bookmark);
	m->my_computer_item->addChild(m->bookmarks_root_item);

	return m->root_dir_item;
}

FileSystemProviderPtr MainWindow::newFileSystemPtr(ItemIdList iidl)
{
	FileSystemProviderPtr fs;

#ifdef Q_OS_WIN
	if (iidl.type() == ItemIdList::Type::PATH) {
		QString path = iidl.path();
		if (path.startsWith("//") && path.indexOf("//", 2) > 2) {
			if (path.startsWith(prefix_mycomputer)) {
				iidl = {m->my_computer_item->iidl()};
				return std::make_shared<BasicFileSystemProvider>(ItemIdList{});
			} else if (path.startsWith(prefix_iidl)) {
				iidl = {QByteArray::fromHex(path.mid(prefix_iidl.size()).toUtf8())};
			}
		}
	}
	if (WindowsFileSystemProvider::isPhysicalFilesystemFolder(iidl)) {
		if (iidl.size() >= 2) {
			QString path;
			if (iidl[0] == '/' && iidl[1] == '/') {
				path = QString::fromUtf8(iidl.data() + 2, iidl.size() - 2);
			} else {
				path = global->shapi->pathFromList((ITEMIDLIST *)iidl.data());
			}
			if (!path.isEmpty()) {
				fs = std::make_shared<BasicFileSystemProvider>(("//" + path).toUtf8());
			}
		}
	}
#endif
	if (!fs) {
		fs = m->fs_factory->create(iidl);
	}

	return fs;
}

void MainWindow::fetchSubFolders(FolderTreeItem *parent)
{
	if (!parent) return;
	Kind kind = (Kind)parent->data(0, KindRole).toInt();
	if (kind == Kind::Directory || kind == Kind::SubDirectory) {
		if (parent->childCount() == 1) {
			FolderTreeItem *child = parent->child(0);
			if ((Kind)child->data(0, KindRole).toInt() == Kind::Placeholder) {
				ItemIdList iidl = parent->data(0, IidlRole).value<ItemIdList>();
				FileSystemProviderPtr fs = newFileSystemPtr(iidl);
				makeTree(fs.get(), parent);
			}
		}
	}
}

void MainWindow::updateFileView()
{
	auto item = ui->treeView->currentItem();
	if (!item) return;

	{
		int i = m->fetch_location_threads.size();
		while (i > 0) {
			i--;
			auto t = m->fetch_location_threads[i];
			t->requestInterruption();
		}
	}

	LocationData loc;
	loc.kind = (Kind)item->data(0, KindRole).toInt();
	loc.iidl = item->data(0, IidlRole).value<ItemIdList>();
	loc.path = item->data(0, PathRole).toString();
	m->current_location = loc;

	if (loc.isDir()) {
		auto fs = newFileSystemPtr(loc.iidl);
		if (!fs->hasReadPermission()) {
			m->current_location.kind = Kind::NotPermittedDirectory;
			refreshFileList2(loc, true);
			return;
		}
	}

	std::shared_ptr<FetchLocationThread> t = std::make_shared<FetchLocationThread>();
	t->data = loc;
	m->fetch_location_threads.push_back(t);
	connect(t.get(), &FetchLocationThread::done, this, &MainWindow::onRefreshFileListDone);
	t->start();
}

void MainWindow::updateViews()
{
	ItemIdList iidl = currentIIDL();
	makeTreeCompletely();
	openDir(iidl);
}

QString MainWindow::nameText(FileInfo2 const &info)
{
	return info.name;
}

FolderTreeItem *MainWindow::findBookmarkTreeItem(const QString &path, FolderTreeItem *item)
{
	if (item->data(0, PathRole).toString() == path) {
		return item;
	}
	for (int i = 0; i < item->childCount(); i++) {
		auto p = findBookmarkTreeItem(path, item->child(i));
		if (p) return p;
	}
	return nullptr;
}

void MainWindow::moveToParent()
{
	auto item = ui->treeView->currentItem();
	if (!item) return;
	auto parent = item->parent();
	if (!parent) return;
	m->last_location = currentPath();
	ui->treeView->setCurrentItem(parent);
}

void MainWindow::openTableItem(QModelIndex const &index)
{
	auto Data = [&](int role){
		return ui->thumbnailView->model()->data(index, role);
	};
	QString path = Data(PathRole).toString();
	QFileInfo fi(path);
	if (fi.isDir()) {
		openDir(path);
		return;
	}
	if (fi.isFile()) {
		QDesktopServices::openUrl(QUrl::fromLocalFile(path));
		return;
	}
	QString url = Data(UrlRole).toString();
	if (url.startsWith(prefix_bookmark) && url.endsWith(" /")) {
		auto item = findBookmarkTreeItem(url, m->bookmarks_root_item);
		ui->treeView->setExpanded(item, true);
		ui->treeView->setCurrentItem(item);
		updateFileView();
		return;
	}
	if (url.startsWith("http://") || url.startsWith("https://")) {
		QDesktopServices::openUrl(QUrl(url));
		return;
	}
}

void MainWindow::toggleTreeItemExpansion()
{
	auto item = ui->treeView->currentItem();
	if (!item) return;
	ui->treeView->setExpanded(item, !ui->treeView->isExpanded(item));
}

void MainWindow::renameFolder(QString const &location)
{
	RenameDialog dlg(RenameDialog::Folder, location, this);
	if (dlg.exec() == QDialog::Accepted) {

	}
}

void MainWindow::renameFile(QString const &path)
{
	RenameDialog dlg(RenameDialog::File, path, this);
	if (dlg.exec() == QDialog::Accepted) {

	}
}

void MainWindow::onRename()
{
	QString path;
	auto focuswidget = QApplication::focusWidget();
	if (focuswidget == ui->treeView) {
		path = currentLocation();
	} else if (focuswidget == ui->tableView) {
		path = currentFilePath();
	}
	QFileInfo info(path);
	if (info.isDir()) {
		renameFolder(path);
	} else {
		renameFile(path);
	}
}

bool MainWindow::acceptKeyEvent(QKeyEvent *event)
{
	int key = event->key();
	switch (key) {
	case Qt::Key_Period:
		return false;
	}
	return true;
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
	if (event->type() == QEvent::KeyPress) {
		QKeyEvent *ke = (QKeyEvent *)event;
		if (focusWidget() == ui->treeView) {
			if (ke->key() == '*') {
				return true; // suppress tree expansion
			}
			if (ke->key() == Qt::Key_Left || ke->key() == Qt::Key_Enter || ke->key() == Qt::Key_Return) {
				if (ui->treeView->currentItem() == m->my_computer_item) {
					if (ui->treeView->isExpanded(m->my_computer_item)) {
						return true; // suppress tree collapse
					}
				}
			}
		}
	}
	return QMainWindow::eventFilter(watched, event);
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
	QWidget *focuswidget = QApplication::focusWidget();
	int key = event->key();
	if (QApplication::activeModalWidget()) {
		// nop:
	} else {
		switch (key) {
		case Qt::Key_Tab:
		case Qt::Key_Backtab:
			if (focuswidget == ui->treeView) {
				switch (viewmode()) {
				case List:
					ui->stackedWidget_fileview->setCurrentWidget(ui->page_list);
					ui->tableView->setFocus();
					break;
				case Thumbnail:
					ui->stackedWidget_fileview->setCurrentWidget(ui->page_thumbnail);
					ui->thumbnailView->setFocus();
					break;
				}
			} else if (focuswidget == ui->tableView) {
				setFocusFolderTree();
			} else if (focuswidget == ui->thumbnailView) {
				setFocusFolderTree();
			} else if (focuswidget == ui->lineEdit_address) {
				setFocusFolderTree();
			}
			break;
		case Qt::Key_Enter:
		case Qt::Key_Return:
			if (focuswidget == ui->treeView) {
				toggleTreeItemExpansion();
				return;
			}
			if (focuswidget == ui->tableView) {
				openTableItem(ui->tableView->currentIndex());
				return;
			}
			if (focuswidget == ui->thumbnailView) {
				openTableItem(ui->thumbnailView->currentIndex());
				return;
			}
			if (focuswidget == ui->lineEdit_address) {
				QString text = ui->lineEdit_address->text();
				openDir(text);
				return;
			}
			break;
		case Qt::Key_Escape:
			ui->frame_tool_bar->setVisible(false);
			setFocusFolderTree();
			event->accept();
			return;
		case Qt::Key_Backspace:
			if (focuswidget == ui->tableView || focuswidget == ui->thumbnailView) {
				moveToParent();
			}
			return;
		case Qt::Key_B:
			if (event->modifiers() & Qt::AltModifier) {
				toggleBookbarkBarVisible();
				return;
			}
			break;
		case Qt::Key_D:
			if (event->modifiers() & Qt::AltModifier) {
				setAddressBarVisible(true);
				return;
			}
			break;
		case Qt::Key_F2:
			onRename();
			return;
		case Qt::Key_F5:
			updateViews();
			return;
		case Qt::Key_Period:
			if (focuswidget == ui->lineEdit_address) {
				return;
			}
			setHiddenFilesVisible(!hiddenFilesVisible());
			return;
		}
	}
}

void MainWindow::setFocusFolderTree()
{
	setAddressBarVisible(false);
	ui->treeView->setFocus();
}

QImage MainWindow::queryThumbnail(QString const &path)
{
#ifdef Q_OS_WIN
	QFileInfo fi(path);
	if (fi.isDir()) {
		ItemIdList iidl = global->shapi->parseDisplayName(path);
		if (WindowsFileSystemProvider::isPhysicalFilesystemFolder(iidl)) {
			return WindowsFileSystemProvider::getStockFolderIcon();
		}
	}
#endif

	auto task = m->thumbnail_loader.query(path);
	if (task) {
		return task->image;
	}
	return {};
}

void MainWindow::thumbnailReady(std::shared_ptr<ThumbnailLoader::Entiry> entity)
{
	ui->thumbnailView->updateThumbnail(entity->path, entity->image);
}

void MainWindow::setHiddenFilesVisible(bool f)
{
	m->hidden_files_visible = f;
	{
		auto item = ui->treeView->currentItem();
		if (item) {

			while (item->childCount() > 0) {
				delete item->takeChild(item->childCount() - 1);
			}

			auto placeholder = new_FolderTreeItem();
			placeholder->setData(0, KindRole, (int)Kind::Placeholder);
			item->addChild(placeholder);

			fetchSubFolders(item);
		}
	}
	updateFileView();
}

bool MainWindow::hiddenFilesVisible() const
{
	return m->hidden_files_visible;
}

QString MainWindow::sizeText(FileInfo2 const &info)
{
	return QString::number(info.size);
}

QString MainWindow::typeText(FileInfo2 const &info)
{
	if (info.isdir) {
		// nop
	} else {
		QString name = info.name;
		int i = name.lastIndexOf('.');
		if (i >= 0) {
			return name.mid(i + 1);
		}
	}
	return QString();
}

QString MainWindow::modifiedText(FileInfo2 const &info)
{
	QDateTime dt = info.modified;
	if (dt.isValid()) {
		int year = dt.date().year();
		int month = dt.date().month();
		int day = dt.date().day();
		int hour = dt.time().hour();
		int minute = dt.time().minute();
		int second = dt.time().second();
		return QString().asprintf("%04u-%02u-%02u %02u:%02u:%02u", year, month, day, hour, minute, second);
	}
	return QString();
}

void MainWindow::sortFileInfoList(std::vector<FileInfo2> *list)
{
	std::sort(list->begin(), list->end(), [](FileInfo2 const &l, FileInfo2 const &r){
		return [](FileInfo2 const &l, FileInfo2 const &r){
			if (l.isdir && r.isdir) {
				if (l.path.isEmpty() || r.path.isEmpty()) {
					int n = std::max(l.iidl.size(), r.iidl.size());
					for (int i = 0; i < n; i++) {
						unsigned char c = (i < l.iidl.size()) ? l.iidl[i] : 0;
						unsigned char d = (i < r.iidl.size()) ? r.iidl[i] : 0;
						if (c < d) return -1;
						if (c > d) return 1;
					}
				}
			} else {
				if (l.isdir) return -1;
				if (r.isdir) return 1;
			}
			return QString::compare(l.path, r.path, Qt::CaseInsensitive);
		}(l, r) < 0;
	});
}

void MainWindow::clearIconCache()
{
	m->icon_cache.clear();
}

QIcon MainWindow::getIcon(const FileInfo2 &info)
{
	QFileIconProvider const &iconprov = m->icon_provider;

	if (!info.icon.isNull()) {
		return info.icon;
	}
	if (info.isdir) {
		return iconprov.icon(QFileIconProvider::Folder);
	} else {
		auto it = m->icon_cache.find(info.path);
		if (it != m->icon_cache.end()) {
			return it->second;
		}
		QFileInfo info_(info.path);
		if (info_.isFile()) {
			QIcon icon = iconprov.icon(info_);
			if (!icon.isNull()) {
				m->icon_cache[info.path] = icon;
				return icon;
			}
		}
		return iconprov.icon(QFileIconProvider::File);
	}
}

void MainWindow::onRefreshFileListDone(LocationData const &loc)
{
	refreshFileList2(loc, false);
}

void MainWindow::refreshFileList2(LocationData const &loc, bool force)
{
	QString select_location = m->last_location;
	int select_row = 0;

	m->last_location = loc.path; // update current location

	{
		int i = m->fetch_location_threads.size();
		while (i > 0) {
			i--;
			auto t = m->fetch_location_threads[i];
			if (!t->isRunning()) {
				m->fetch_location_threads.erase(m->fetch_location_threads.begin() + i);
			}
		}
	}

	ui->tableView->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
	ui->tableView->verticalHeader()->setDefaultSectionSize(20);

	if (!force) {
		if (loc != m->current_location) return;
	}

	struct DeferResetModel {
		FileItemModel *model;
		DeferResetModel(FileItemModel *model)
			: model(model)
		{
			model->beginResetModel();
		}
		~DeferResetModel() {
			model->endResetModel();
		}
	};

	auto *model = fileitemmodel();

	QElapsedTimer t;
	t.start();

	{
		DeferResetModel defer1(model);

		model->items.clear();

		if (loc.isDir()) {
			for (int row = 0; row < loc.files.size(); row++) {
				FileInfo2 const &info = loc.files[row];
				if (!m->hidden_files_visible && info.ishidden) continue;
				FileItemModel::Item fileitem;
				fileitem.info = info;
				fileitem.name = nameText(info);
				fileitem.path = info.path;
				if (info.isdir) {
					fileitem.size = -1;
					fileitem.type = "<DIR>";
					if (fileitem.path == select_location) {
						select_row = model->items.size();
					}
				} else {
					fileitem.size = info.size;
					fileitem.type = typeText(info);
				}
				fileitem.modified = info.modified;
				model->items.push_back(fileitem);
			}
			ui->tableView->reset();
		} else if (loc.kind == Kind::ChromeBookmark) {
			auto it = m->bookmark_items.find(loc.path);
			if (it == m->bookmark_items.end()) return;
			QList<BookmarkInfo> const &items = it->second;
			for (int row = 0; row < items.size(); row++) {
				FileItemModel::Item fileitem;
				BookmarkInfo const &info = items[row];
				fileitem.name = info.name;
				fileitem.path = info.url;
				fileitem.icon_ = info.url.endsWith(" /") ? m->default_folder_icon : m->default_file_icon;
				model->items.push_back(fileitem);
			}
		}
	}

	// qDebug() << t.elapsed();

	m->file_item_model.setKind(loc.kind);
	ui->tableView->setKind(loc.kind);
	ui->thumbnailView->setKind(loc.kind);
	ui->tableView->setLocation(loc.path);
	ui->thumbnailView->setLocation(loc.path);

	int n = model->rowCount();
	setStatusBarText(QString("%1 items").arg(n));

	//

	switch (viewmode()) {
	case List:
		ui->tableView->selectRow(select_row);
		break;
	case Thumbnail:
		ui->thumbnailView->selectRow(select_row);
		break;
	}
}

QString MainWindow::currentLocation()
{
	QString loc;
	auto item = ui->treeView->currentItem();
	if (item) {
		loc = item->data(0, PathRole).toString();
		if (loc.isEmpty()) {
			ItemIdList iidl = item->data(0, IidlRole).value<ItemIdList>();
			if (iidl.type() == ItemIdList::Type::PATH) {
				loc = iidl.path();
			} else if (iidl.type() == ItemIdList::Type::WIN_SHELL_ITEMIDLIST) {
				int n = iidl.size();
				char *p = (char *)alloca(n * 2 + 1);
				*p = 0;
				for (int i = 0; i < n; i++) {
					sprintf(p + i * 2, "%02x", (uint8_t)iidl.data()[i]);
				}
				loc = QString(prefix_iidl) + p;
			}
		}
	}
	return loc;
}

QString MainWindow::currentFilePath()
{
	auto index = ui->tableView->currentIndex();
	return ui->tableView->model()->data(index, PathRole).toString();
}

void MainWindow::on_treeView_currentItemChanged(FolderTreeItem *current, FolderTreeItem *previous)
{
	qDebug() << Q_FUNC_INFO;

	(void)current;
	(void)previous;

	ui->treeView->scrollTo(ui->treeView->indexFromItem(current), QTreeView::EnsureVisible);

	{
		auto item = ui->treeView->currentItem();
		if (item) {
			ItemIdList iidl = item->data(0, IidlRole).value<ItemIdList>();
			setCurrentIIDL(iidl);
		}
	}

	QString loc = currentLocation();
	if (loc.isEmpty()) return;

	m->thumbnail_loader.clearRequests();

	bool ok = false;
	if (loc.startsWith(prefix_mycomputer)) {
		ok = true;
	} else if (loc.startsWith(prefix_bookmark)) {
		ok = true;
	} else if (loc.startsWith(prefix_iidl)) {
		ok = true;
	} else {
#ifdef Q_OS_WIN
		loc = loc.replace('/', '\\');
#endif
		QFileInfo info(loc);
		if (info.isDir()) {
			ok = true;
		}
	}
	if (ok) {
		updateFileView();
	}
	ui->lineEdit_address->setText(loc);
}

#ifdef Q_OS_WIN
FolderTreeItem *MainWindow::openDirWindows(ItemIdList const &iidl)
{
	std::vector<ItemIdList> list;
	if (iidl.type() == ItemIdList::Type::WIN_SHELL_ITEMIDLIST) {
		size_t pos = 0;
		while (pos + 1 < iidl.size()) {
			char const *ptr = iidl.data() + pos;
			uint16_t len = *(uint16_t *)ptr;
			if (len == 0) break;
			pos += len;
			QByteArray iidl2(iidl.data(), pos);
			iidl2.push_back((char)0); // two zeros for terminating the ITEMIDLIST
			iidl2.push_back((char)0);
			list.push_back(iidl2);
		}
	}

	FolderTreeItem *item;

	for (size_t i = 0; i < list.size(); i++) {
		item = ui->treeView->itemFromIidl(list[i]);
		if (item) {
			fetchSubFolders(item);
			ui->treeView->setExpanded(item, true);
		}
	}

	return item;
}
#else
FolderTreeItem *MainWindow::openDirPosix(ItemIdList const &iidl)
{
	std::vector<ItemIdList> list;
	{
		ItemIdList current = iidl;
		while (!current.empty()) {
			list.push_back(current);
			QByteArray iidl3 = current.iidl();
			int i = iidl3.lastIndexOf('/');
			if (i >= 0) {
				current = ItemIdList(iidl3.mid(0, i));
			} else {
				current = ItemIdList();
			}
		}
	}
	{
		FolderTreeItem *item = m->root_dir_item;
		if (item) {
			ui->treeView->setExpanded(item, true);
			size_t i = list.size();
			while (i > 0) {
				i--;
				int n = item->childCount();
				bool found = false;
				for (int j = 0; j < n; j++) {
					FolderTreeItem *child = item->child(j);
					ItemIdList child_iidl = child->data(0, IidlRole).value<ItemIdList>();
					if (child_iidl.iidl() == list[i].iidl()) {
						fetchSubFolders(child);
						ui->treeView->setExpanded(child, true);
						item = child;
						if (i == 0) {
							return item;
						}
						found = true;
						break;
					}
				}
				if (!found) {
					break;
				}
			}
		}
	}


	return {};
}
#endif

QString MainWindow::currentPath() const
{
	auto item = ui->treeView->currentItem();
	if (!item) return QString();
	return item->data(0, PathRole).toString();
}

ItemIdList MainWindow::currentIIDL() const
{
	return m->current_iidl;
}

void MainWindow::setCurrentIIDL(ItemIdList iidl)
{
#ifdef Q_OS_WIN
	if (iidl.type() == ItemIdList::Type::PATH) {
		QString path = iidl.path();
		if (path.startsWith("//") && path.indexOf("//", 2) > 2) {
			// nop
		} else {
			iidl = global->shapi->parseDisplayName(path);
		}
	}
#endif

	m->current_iidl = iidl;
}

void MainWindow::setCurrentDir(const QString &dir)
{
	setCurrentIIDL(dir);
}

void MainWindow::openDir(ItemIdList const &iidl)
{
	setFocusFolderTree();

	setCurrentIIDL(iidl);
	FolderTreeItem *item;
#ifdef Q_OS_WIN
	item = openDirWindows(iidl);
#else
	item = openDirPosix(iidl);
#endif
	if (item) {
		ui->treeView->setFocus();
		ui->treeView->setCurrentItem(item);
		ui->treeView->setExpanded(item, true);
	}
}

void MainWindow::setAddressBarVisible(bool visible)
{
	if (visible) {
		ui->frame_tool_bar->setVisible(true);
		ui->lineEdit_address->setFocus();
		ui->lineEdit_address->selectAll();
	} else {
		ui->frame_tool_bar->setVisible(false);
	}
}

void MainWindow::toggleBookbarkBarVisible()
{
	ui->frame_bookmark_bar->setVisible(!ui->frame_bookmark_bar->isVisible());
}

void MainWindow::makeBookmarkMap(QString const &path, QJsonValue v, int depth, FolderTreeItem *parent)
{
	if (v.isObject()) {
		QJsonObject o = v.toObject();
		QJsonValue v2 = o.value("children");
		if (v2.isArray()) {
			QJsonValue v3 = o.value("name");
			QString name = v3.toString();
			QString childpath = path;
			if (!path.endsWith('/')) {
				childpath += '/';
			}
			childpath += name;
			m->bookmark_items[path].push_back(BookmarkInfo(name, childpath));
			auto child = new_FolderTreeItem();
			child->setText(0, name);
			child->setData(0, KindRole, (int)Kind::ChromeBookmark);
			child->setData(0, PathRole, childpath);
			parent->addChild(child);
			QJsonArray children = v2.toArray();
			for (QJsonValue const &v4 : children) {
				makeBookmarkMap(childpath, v4, depth + 1, child);
			}
			return;

		}

		QJsonValue v5 = o.value("name");
		if (v5.isString()) {
			QString url = o.value("url").toString();
			QString name = v5.toString();
			m->bookmark_items[path].push_back(BookmarkInfo(name, url));
			return;
		}
	}
}

void MainWindow::on_tableView_customContextMenuRequested(const QPoint &pos)
{
	QPoint pt = QCursor::pos() + QPoint(8, -8);
	QMenu menu;
	QAction *a_create_folder = menu.addAction(tr("Create folder"));
	QAction *a = menu.exec(pt);
	if (a) {
		if (a == a_create_folder) {
			return;
		}
	}
}

void MainWindow::fetchBookmarks()
{
#ifdef Q_OS_MAC
	QString path = "~/Library/Application Support/Google/Chrome/Default/Bookmarks";
#endif
#ifdef Q_OS_WIN
	QString datadir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
	QString path = datadir / "Google/Chrome/User Data/Default/Bookmarks";
#endif
#ifdef Q_OS_LINUX
	QString path = "~/.config/google-chrome/Default/Bookmarks";
	path = misc::realpath(path);
#endif

	while (m->bookmarks_root_item->childCount() > 0) {
		delete m->bookmarks_root_item->takeChild(m->bookmarks_root_item->childCount() - 1);
	}
	m->bookmark_items.clear();
	auto parent = m->bookmarks_root_item;
	QFile file(path);
	if (file.open(QFile::ReadOnly)) {
		QByteArray ba = file.readAll();
		QJsonDocument doc = QJsonDocument::fromJson(ba);
		QJsonObject o = doc.object();
		QJsonObject roots = o.value("roots").toObject();
		QStringList keys = roots.keys();
		for (QString const &key : keys) {
			QJsonValue v = roots.value(key);
			QJsonObject child = v.toObject();
			if (!child.isEmpty()) {
				makeBookmarkMap(prefix_bookmark, child, 0, parent);
			}
		}
	}
	ui->treeView->setExpanded(m->bookmarks_root_item, true);
}

std::vector<FileInfo2> MainWindow::windowsMyComputerFiles()
{
	std::vector<FileInfo2> files;
#ifdef Q_OS_WIN
	auto fs = FileSystemProviderPtr(new WindowsFileSystemProvider());
	auto fileitor = fs.get();
	while (fileitor->fetch()) {
		FileInfo2 info = fileitor->fileInfo();
		QString name = info.name;
		if (info.isdir) {
			if (name == "." || name == "..") {
				continue;
			}
			files.push_back(info);
		} else {

		}
	}
	sortFileInfoList(&files);
#endif
	return files;
}

void MainWindow::setAppSettings(const ApplicationSettings &appsettings)
{
	global->appsettings = appsettings;
}

void MainWindow::on_action_edit_sttings_triggered()
{
	SettingsDialog dlg(this);
	if (dlg.exec() == QDialog::Accepted) {
		ApplicationSettings const &newsettings = dlg.settings();
		setAppSettings(newsettings);
	}
}

void MainWindow::on_action_view_detailed_triggered()
{
	setViewMode(List);
}


void MainWindow::on_action_view_thumbnails_triggered()
{
	setViewMode(Thumbnail);
}

void MainWindow::on_action_view_hide_item_triggered()
{
	QString path = currentPath();
}

void MainWindow::on_treeView_itemExpanded(FolderTreeItem *item)
{
	if (item) {
		fetchSubFolders(item);
	}
}

void MainWindow::on_treeView_collapsed(const QModelIndex &index)
{
	if (ui->treeView->itemFromIndex(index) == m->my_computer_item) {
		ui->treeView->setExpanded(m->my_computer_item, true); // 閉じさせない
	}
}

void MainWindow::on_action_test_triggered()
{
	auto *item = ui->treeView->currentItem();
	if (item) {
		qDebug() << ui->lineEdit_address->text() << item->text();
	}
}

