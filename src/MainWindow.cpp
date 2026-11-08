
#include "MainWindow.h"
#include "ui_MainWindow.h"
#include "ApplicationGlobal.h"
#include "BasicFileSystemProvider.h"
#include "FetchLocationThread.h"
#include "RenameDialog.h"
#include "SettingsDialog.h"
#include "StatusLabel.h"
#include "common/joinpath.h"
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QFileIconProvider>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QTableWidgetItem>
#include <QTimer>
#include <QUrl>
#include "common/realpath.h"
#include "common/str.h"
#include "common/misc.h"
#include "common/q/helper.h""
#include "IncrementalSearchHelper.h"
#include <cstring>
#include <unordered_set>

#ifdef Q_OS_WIN
#include "NetworkDiscoveryThread.h"
#include "WindowsFileSystemProvider.h"
#include "WindowsShellAPI.h"
#include <common/q/helper.h>
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
	bool folder = false;
	BookmarkInfo(QString const &name = QString(), QString const &url = QString(), bool folder = false)
		: name(name)
		, url(url)
		, folder(folder)
	{
	}
};

struct MainWindow::Private {
	FileItemModel file_item_model;

	ItemIdList current_iidl;
	ThumbnailLoader thumbnail_loader;
	std::unordered_set<QString> thumbnail_paths;
	StatusLabel *status_label;
	QTimer update_list_timer;
	QTimer network_changed_timer;
	bool suppress_current_changed = false;
	FileSystemProviderPtr fs_factory;
	FolderTreeItem *my_computer_item = nullptr;
	FolderTreeItem *drive_root = nullptr;
	FolderTreeItem *root_dir_item = nullptr; // for POSIX=/, for Windows=Desktop

	FolderTreeItem *bookmarks_root_item = nullptr;
	QString bookmark_file_path;

	QFileIconProvider icon_provider;
	QIcon default_file_icon;
	QIcon default_folder_icon;

	std::map<QString, QList<BookmarkInfo>> bookmark_items;

	LocationData current_location;
	QString last_location;
	QList<std::shared_ptr<FetchLocationThread>> fetch_location_threads;
	quint64 fetch_generation = 0;
	bool location_loading = false;
	struct ItemIdentity {
		ItemIdList iidl;
		QString path;
		static ItemIdentity fromIndex(QModelIndex const &index)
		{
			return {index.data(IidlRole).value<ItemIdList>(), index.data(PathRole).toString()};
		}
		bool matches(QModelIndex const &index) const
		{
			return !path.isEmpty() ? path == index.data(PathRole).toString()
				: !iidl.empty() && iidl == index.data(IidlRole).value<ItemIdList>();
		}
	};
	struct ReloadSelection {
		LocationData location;
		ItemIdentity current;
		std::vector<ItemIdentity> selected;
		bool matchesLocation(LocationData const &other) const
		{
			// A filesystem PIDL can acquire new metadata during a refresh.
			return !location.path.isEmpty() ? location.path == other.path : location == other;
		}
	};
	std::optional<ReloadSelection> reload_selection;

	bool hidden_files_visible = false;

	std::map<QString, QIcon> icon_cache;

	std::set<ItemIdList> iidl_hide_set;
#ifdef Q_OS_WIN
	NetworkDiscoveryThread network_discovery_thread;
#endif
	
	MainWindow::FilterTarget filter_target = MainWindow::FilterTarget::FolderTreeSearch;
	StatusLabel *status_bar_label;
	
	QWidget *focus_widget = nullptr;
};

static QString getDisplayName(FileInfo2 const &info)
{
	return info.name;
}

#ifdef Q_OS_WIN
static std::optional<std::vector<ItemIdList>> splitShellItemIdList(ItemIdList const &iidl)
{
	if (iidl.type() != ItemIdList::Type::WIN_SHELL_ITEMIDLIST || iidl.size() < 2) return {};
	std::vector<ItemIdList> parts;
	qsizetype pos = 0;
	while (pos + 2 <= iidl.size()) {
		uint16_t length;
		std::memcpy(&length, iidl.data() + pos, sizeof(length));
		if (length == 0) {
			if (pos + 2 != iidl.size()) return {};
			return parts;
		}
		if (length < 2 || length > iidl.size() - pos - 2) return {};
		pos += length;
		QByteArray prefix(iidl.data(), pos);
		prefix.append(2, '\0');
		parts.emplace_back(prefix);
	}
	return {};
}
#endif

void MainWindow::addPlaceholder(FolderTreeItem *item)
{
	auto placeholder = std::make_unique<FolderTreeItem>();
	placeholder->setData(0, KindRole, (int)Kind::Placeholder);
	ui->treeView->setExpanded(item, false);
	if (item->addChild(placeholder.get())) placeholder.release();
}

MainWindow::MainWindow(QWidget *parent)
	: QMainWindow(parent)
	, m(new Private())
	, ui(new Ui::MainWindow)
{
	ui->setupUi(this);
	
	m->status_bar_label = new StatusLabel(this);
	m->status_bar_label->setAlignment((Qt::Alignment)Qt::TextSingleLine);
	ui->statusBar->addWidget(m->status_bar_label);
	
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
	m->network_changed_timer.setSingleShot(true);
	connect(&m->network_changed_timer, &QTimer::timeout, this, &MainWindow::updateNetworkTree);
	connect(&m->network_discovery_thread, &NetworkDiscoveryThread::filesChanged, this, &MainWindow::onNetworkFilesChanged);

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
	m->network_discovery_thread.stop();
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
	connect(&m->thumbnail_loader, &ThumbnailLoader::failed, this, [this](QString const &, QString const &message) {
		setStatusBarText(message);
	});
	m->thumbnail_loader.start();
	
	ui->stackedWidget_fileview->setCurrentWidget(ui->page_list);
	
	setFocusFolderTree();
	ui->treeView->setExpanded(m->drive_root, true);
	ui->treeView->setCurrentItem(m->drive_root);
}

#ifdef Q_OS_WIN
void MainWindow::onNetworkFilesChanged()
{
	// 探索スレッドからの通知は頻繁なので、まとめてから反映する
	if (!m->network_changed_timer.isActive()) {
		m->network_changed_timer.start(500);
	}
}

/**
 * @brief ネットワーク探索の結果をツリーと右ペインに反映する
 *
 * ツリーの選択状態を変えずに更新する。
 * 選択が「ネットワーク」の配下にあるときは、再構築中の currentItemChanged を抑止して選択を復元する。
 */
void MainWindow::updateNetworkTree()
{
	if (isIncrementalSearching()) return;

	FolderTreeItem *net = ui->treeView->itemFromIidl(FileItem::getNetwork().idlist());
	if (!net) return;

	FolderTreeItem *cur = ui->treeView->currentItem();
	const bool cur_is_net = (cur == net);
	bool cur_in_net = false;
	for (FolderTreeItem *p = cur; p; p = p->parent()) {
		if (p == net) {
			cur_in_net = true;
			break;
		}
	}
	ItemIdList cur_iidl;
	if (cur) {
		cur_iidl = cur->data(0, IidlRole).value<ItemIdList>();
	}

	bool loaded = false; // 子の一覧を一度でも作ったか
	for (int i = 0; i < net->childCount(); i++) {
		if ((Kind)net->child(i)->data(0, KindRole).toInt() != Kind::Placeholder) {
			loaded = true;
			break;
		}
	}

	bool restored = true;
	if (loaded) {
		if (!cur_in_net && !ui->treeView->isExpanded(net)) {
			// 閉じている間は作り直さず、次に開いたときに読み直させる
			while (net->childCount() > 0) {
				delete net->takeChild(net->childCount() - 1);
			}
			addPlaceholder(net);
		} else {
			m->suppress_current_changed = true;
			fetchSubFolders(net, false);
			if (cur_is_net) {
				ui->treeView->setCurrentItem(net);
			} else if (cur_in_net) {
				FolderTreeItem *item = ui->treeView->itemFromIidl(cur_iidl);
				if (item) {
					ui->treeView->setCurrentItem(item);
				} else {
					restored = false; // 選択中のホストが消えた
				}
			} else if (cur) {
				ui->treeView->setCurrentItem(cur);
			}
			m->suppress_current_changed = false;
		}
	}

	if (!restored) {
		on_treeView_currentItemChanged(ui->treeView->currentItem(), nullptr);
	} else if (cur_is_net) {
		updateFileView();
	}
}
#endif

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
	if (iidl.size() >= 2 && iidl[0] == '/' && iidl[1] == '/') { // network drive
		QString path2 = iidl.path();
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
	qDebug() << Q_FUNC_INFO;

	if (isIncrementalSearching()) return;

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
					dirs.push_back(info);
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

	if (!parent->clearChildren()) return;

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
	QScopedValueRollback<bool> suppress(m->suppress_current_changed, true);
	QSignalBlocker block(ui->treeView);
	clearAllFilters();
	ui->treeView->clear();

	FileInfo2 info = firstFileInfo();
	auto fs = m->fs_factory->create(info.iidl);
	
	m->root_dir_item = new_FolderTreeItem();
	setTreeViewSubDirItemData(m->root_dir_item, info);

	m->my_computer_item = new_FolderTreeItem();
	m->my_computer_item->setText(0, tr("My Computer"));
	m->my_computer_item->setData(0, KindRole, (int)Kind::Directory);
	m->my_computer_item->setData(0, PathRole, (QS)prefix_mycomputer);
	ui->treeView->addTopLevelItem(m->my_computer_item);
	m->my_computer_item->addChild(m->root_dir_item);
	m->my_computer_item->setData(0, IidlRole, QVariant::fromValue<ItemIdList>((QS)prefix_mycomputer));

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
	m->bookmarks_root_item->setData(0, PathRole, (QS)prefix_bookmark);
	m->bookmarks_root_item->setData(0, IidlRole, QVariant::fromValue(ItemIdList((QS)prefix_bookmark)));
	m->my_computer_item->addChild(m->bookmarks_root_item);
	fetchBookmarks();

	return m->root_dir_item;
}

FileSystemProviderPtr MainWindow::newFileSystemPtr(ItemIdList iidl)
{
	return createFileSystemProvider(iidl);
}

void MainWindow::fetchSubFolders(FolderTreeItem *parent, bool check_placeholder)
{
	if (isIncrementalSearching()) return;
	if (!parent) return;
	if (parent == m->my_computer_item) return;
	Kind kind = (Kind)parent->data(0, KindRole).toInt();
	if (kind == Kind::Directory || kind == Kind::SubDirectory) {
		auto HasPlaceholder = [&](){
			if (parent->childCount() == 1) {
				FolderTreeItem *child = parent->child(0);
				if ((Kind)child->data(0, KindRole).toInt() == Kind::Placeholder) {
					return true;
				}
			}
			return false;
		};
		if (!check_placeholder || HasPlaceholder()) {
			ItemIdList iidl = parent->data(0, IidlRole).value<ItemIdList>();
			FileSystemProviderPtr fs = newFileSystemPtr(iidl);
			makeTree(fs.get(), parent);
		}
	}
}

void MainWindow::refreshFileList2(LocationData const &loc, bool force)
{
	if (!force && loc != m->current_location) return;
	m->location_loading = false;
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

	auto *model = fileitemmodel();


	QElapsedTimer t;
	t.start();

	std::vector<FileItemModel::Item> items;
	{
		if (loc.isDir()) {
			items.reserve(loc.files.size());
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
				} else {
					fileitem.size = info.size;
					fileitem.type = typeText(info);
				}
				fileitem.modified = info.modified;
				items.push_back(std::move(fileitem));
			}
		} else if (loc.kind == Kind::ChromeBookmark) {
			auto it = m->bookmark_items.find(loc.path);
			if (it != m->bookmark_items.end()) for (BookmarkInfo const &info : it->second) {
				FileItemModel::Item fileitem;
				fileitem.name = info.name;
				fileitem.path = info.url;
				fileitem.info.name = info.name;
				fileitem.info.path = info.url;
				fileitem.info.isdir = info.folder;
				if (info.folder) fileitem.info.iidl = ItemIdList(info.url);
				fileitem.icon_ = info.folder ? m->default_folder_icon : m->default_file_icon;
				items.push_back(std::move(fileitem));
			}
		}
	}
	const QString filter_text = filtertarget() == FilterTarget::FolderTreeSearch ? QString() : getIncrementalSearchText();
	m->thumbnail_paths.clear();
	if (loc.isDir()) for (auto const &item : items) {
		if (!item.info.isdir && !item.path.isEmpty()) m->thumbnail_paths.insert(item.path);
	}
	model->replaceItems(loc.kind, std::move(items), filter_text);
	for (int row = 0; row < model->rowCount(); ++row) {
		if (model->item(row)->info.isdir && model->item(row)->path == select_location) {
			select_row = row;
			break;
		}
	}

	ui->tableView->setKind(loc.kind);
	ui->thumbnailView->setKind(loc.kind);
	ui->tableView->setPath(loc.path);
	ui->thumbnailView->setPath(loc.path);

	updateStatusBarText2();

	//

	if (m->focus_widget == ui->treeView || m->focus_widget == ui->tableView || m->focus_widget == ui->thumbnailView) {
		m->focus_widget->setFocus();
		m->focus_widget = nullptr;
	}
	
	switch (viewmode()) {
	case List:
		ui->tableView->selectRow(select_row);
		break;
	case Thumbnail:
		ui->thumbnailView->selectRow(select_row);
		break;
	}
	restoreReloadSelection();
}

void MainWindow::updateFileView()
{
	const quint64 generation = ++m->fetch_generation;
	m->location_loading = false;
	m->reload_selection.reset();
	m->thumbnail_paths.clear();
	auto item = ui->treeView->currentItem();

	{
		int i = m->fetch_location_threads.size();
		while (i > 0) {
			i--;
			auto t = m->fetch_location_threads[i];
			t->requestInterruption();
		}
	}

	LocationData loc;
	if (!item) {
		m->current_location = loc;
		refreshFileList2(loc, true);
		return;
	}
	loc.kind = (Kind)item->data(0, KindRole).toInt();
	loc.iidl = item->data(0, IidlRole).value<ItemIdList>();
	loc.path = item->data(0, PathRole).toString();
	m->current_location = loc;
	if (loc.kind == Kind::ChromeBookmark) {
		refreshFileList2(loc, true);
		return;
	}
	if (loc.path == (QS)prefix_mycomputer || loc.iidl.path() == (QS)prefix_mycomputer) {
		for (int row = 0; row < item->childCount(); ++row) {
			FolderTreeItem *child = item->child(row);
			if (static_cast<Kind>(child->data(0, KindRole).toInt()) == Kind::Placeholder) continue;
			FileInfo2 info;
			info.name = child->text();
			info.path = location(child);
			info.iidl = child->data(0, IidlRole).value<ItemIdList>();
			info.isdir = true;
			loc.files.push_back(std::move(info));
		}
		refreshFileList2(loc, true);
		return;
	}
	if (!loc.isDir()) {
		refreshFileList2(loc, true);
		return;
	}

#ifdef Q_OS_WIN
	// ネットワーク探索が未完了の間は、右ペインを空にして待つ（完了時に updateNetworkTree が再表示する）
	if (loc.isDir() && !m->network_discovery_thread.isReady() && loc.iidl == FileItem::getNetwork().idlist()) {
		refreshFileList2(loc, true);
		return;
	}
#endif

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
	connect(t.get(), &FetchLocationThread::done, this, [this, generation](LocationData const &location) {
		if (generation == m->fetch_generation) onRefreshFileListDone(location);
	});
	connect(t.get(), &FetchLocationThread::failed, this, [this, generation](LocationData const &location, QString const &message) {
		if (generation != m->fetch_generation || location != m->current_location) return;
		refreshFileList2(location, true);
		setStatusBarText(message);
	});
	m->location_loading = true;
	t->start();
}

void MainWindow::updateViews()
{
	m->thumbnail_loader.clearCache();
	const FilterTarget previous_target = filtertarget();
	const QString previous_filter = previous_target == FilterTarget::FolderTreeSearch ? QString() : getIncrementalSearchText();
	QAbstractItemView *view = viewmode() == List ? static_cast<QAbstractItemView *>(ui->tableView) : ui->thumbnailView;
	Private::ReloadSelection selection;
	selection.location = m->current_location;
	selection.current = Private::ItemIdentity::fromIndex(view->currentIndex());
	if (view->selectionModel()) {
		for (auto const &index : view->selectionModel()->selectedRows()) {
			selection.selected.push_back(Private::ItemIdentity::fromIndex(index));
		}
	}
	ItemIdList iidl = currentIIDL();
	QString previous_location = currentLocation();
	makeTreeCompletely();
	if (previous_location.startsWith((misc::str)prefix_bookmark)
		&& !findBookmarkTreeItem(previous_location, m->bookmarks_root_item)) {
		iidl = ItemIdList((QS)prefix_bookmark);
	}
	if (iidl.empty()) iidl = m->drive_root->iidl();
	openDir(iidl);
	if (selection.matchesLocation(m->current_location)) {
		m->filter_target = previous_target;
		if (!previous_filter.isEmpty()) setIncrementalSearchText(previous_filter);
		m->reload_selection = std::move(selection);
		if (!m->location_loading) restoreReloadSelection();
		updateStatusBarText();
	}
}

void MainWindow::restoreReloadSelection()
{
	if (!m->reload_selection) return;
	auto selection = std::move(*m->reload_selection);
	m->reload_selection.reset();
	if (!selection.matchesLocation(m->current_location)) return;
	QAbstractItemView *view = viewmode() == List ? static_cast<QAbstractItemView *>(ui->tableView) : ui->thumbnailView;
	if (!view->selectionModel()) return;
	view->selectionModel()->clearSelection();
	for (int row = 0; row < fileitemmodel()->rowCount(); ++row) {
		auto index = fileitemmodel()->index(row, 0);
		for (auto const &identity : selection.selected) {
			if (identity.matches(index)) {
				view->selectionModel()->select(index, QItemSelectionModel::Select | QItemSelectionModel::Rows);
				break;
			}
		}
		if (selection.current.matches(index)) {
			view->selectionModel()->setCurrentIndex(index, QItemSelectionModel::NoUpdate);
			view->scrollTo(index);
		}
	}
}

QString MainWindow::nameText(FileInfo2 const &info)
{
	return info.name;
}

FolderTreeItem *MainWindow::findBookmarkTreeItem(const QString &path, FolderTreeItem *item)
{
	if (!item) return nullptr;
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



/**
 * @brief フィルタ文字列を設定する
 * @param text
 */
void MainWindow::setIncrementalSearchText(QString const &text)
{
	m->reload_selection.reset();
	// qDebug() << text;
	FilterTarget ft = filtertarget();

	global->incremental_search_text = text;
	
	if (ft == FilterTarget::FolderTreeSearch) {
		auto *previous = ui->treeView->currentItem();
		const bool had_selection = previous != nullptr;
		const ItemIdList previous_iidl = previous ? previous->iidl() : ItemIdList{};
		{
			// The reset temporarily clears selection. Only the final location
			// should request a new file list, and an unchanged location needs none.
			QSignalBlocker block(ui->treeView);
			ui->treeView->setFilter(text);
		}
		auto *current = ui->treeView->currentItem();
		if (had_selection != (current != nullptr) || (current && current->iidl() != previous_iidl)) {
			on_treeView_currentItemChanged(current, nullptr);
		}
	} else if (ft == FilterTarget::FileListViewSearch) {
		ui->tableView->setFilterText(text);
	} else if (ft == FilterTarget::ThumbnailViewSearch) {
		ui->thumbnailView->setFilterText(text);
	}
}

void MainWindow::clearFileItems()
{
	fileitemmodel()->clearItems();
}

void MainWindow::clearAllFilters()
{
	if (isIncrementalSearching()) {
		setIncrementalSearchText({});
		updateStatusBarText();
	}
}

void MainWindow::openTableItem(QModelIndex const &index)
{
	if (!index.isValid() || index.model() != fileitemmodel()) return;
	// A model reset changes the meaning of the row, so copy its identity first.
	QString path = index.data(PathRole).toString();
	QString url = index.data(UrlRole).toString();
	ItemIdList iidl = index.data(IidlRole).value<ItemIdList>();
	Kind kind = static_cast<Kind>(index.data(KindRole).toInt());
	bool bookmark_list = fileitemmodel()->kind_ == Kind::ChromeBookmark;
	if (path.isEmpty() && url.isEmpty() && iidl.empty()) return;
	m->focus_widget = QWidget::focusWidget();

	clearAllFilters();

	if (path.startsWith((misc::str)prefix_bookmark)) {
		openDir(ItemIdList(path));
		return;
	}
	if (bookmark_list) {
		QUrl address(url);
		if (address.scheme() == "http" || address.scheme() == "https") QDesktopServices::openUrl(address);
		return;
	}
	if ((kind == Kind::Directory || kind == Kind::SubDirectory) && !iidl.empty()) {
		openDir(iidl);
		return;
	}
	QFileInfo fi(path);
	if (fi.isDir()) {
		openDir(path);
		return;
	}
	if (fi.isFile()) {
		QDesktopServices::openUrl(QUrl::fromLocalFile(path));
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

QString MainWindow::currentFilePath()
{
	auto index = ui->tableView->currentIndex();
	return ui->tableView->model()->data(index, PathRole).toString();
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

void MainWindow::updateCurrentFolder()
{
	if (isIncrementalSearching()) return;
	auto item = ui->treeView->currentItem();
	fetchSubFolders(item, false);
}

/**
 * @brief フィルタ文字列を取得する
 * @return
 */
QString MainWindow::getIncrementalSearchText() const
{
	return global->incremental_search_text;
}

bool MainWindow::isIncrementalSearching() const
{
	return !global->incremental_search_text.isEmpty();
}

MainWindow::FilterTarget MainWindow::filtertarget() const
{
	return m->filter_target;
}

void MainWindow::updateStatusBarText()
{
	QString msg_text;
	if (isIncrementalSearching()) {
		QString search_text = getIncrementalSearchText();
		QColor color = incrementalsearch::highlight_bg_color();
		QString s = color.name(QColor::HexRgb);
		msg_text = QString("<div style='background: %1;'>%2: <b>%3</b>&nbsp;</div>")
					   .arg(s)
					   .arg(tr("Search"))
					   .arg(search_text.toHtmlEscaped());
		// msg_format = Qt::TextFormat::RichText;
		m->status_bar_label->setTextFormat(Qt::TextFormat::RichText);
		m->status_bar_label->setText(msg_text);
		m->status_bar_label->show();
	} else {
		m->status_bar_label->setText({});
	}
	updateStatusBarText2();
}

void MainWindow::updateStatusBarText2()
{
	auto *model = fileitemmodel();
	int n = model->rowCount();
	
	if (model->isFiltered()) {
		setStatusBarText(QString("%1/%2 items").arg(n).arg(model->unfilteredCount()));
	} else {
		setStatusBarText(QString("%1 items").arg(n));
	}
}

/**
 * @brief フィルタに文字を追加する
 * @return
 */
bool MainWindow::appendCharToFilterText(QString const &add, MainWindow::FilterTarget ft)
{
	if (add.isEmpty()) return false;
	
	const bool same_pane = (ft == FilterTarget::FolderTreeSearch) == (filtertarget() == FilterTarget::FolderTreeSearch);
	QString filter = same_pane ? getIncrementalSearchText() : QString();
	QString newfilter = incrementalsearch::appendCharToFilterText(filter, add);
	if (newfilter == filter) return false; // if no change, nothing to do
	
	if (!same_pane && isIncrementalSearching()) {
		const ItemIdList selected = ui->treeView->currentItem() ? ui->treeView->currentItem()->iidl() : ItemIdList{};
		{
			QScopedValueRollback<bool> suppress(m->suppress_current_changed, true);
			QSignalBlocker block(ui->treeView);
			clearAllFilters();
			if (!selected.empty()) ui->treeView->setCurrentItem(ui->treeView->itemFromIidl(selected));
		}
		if (selected.empty() && ui->treeView->currentItem()) {
			on_treeView_currentItemChanged(ui->treeView->currentItem(), nullptr);
		}
	}
	m->filter_target = ft;

	setIncrementalSearchText(newfilter);
	
	updateStatusBarText();
	return true;
}

void MainWindow::setFocusFolderTree()
{
	setAddressBarVisible(false);
	ui->treeView->setFocus();
}

bool MainWindow::handleTabKeyPressed()
{
	if (isIncrementalSearching()) {
		return true; // nop
	}
	QWidget *focuswidget = QApplication::focusWidget();
	if (focuswidget == ui->treeView) {
		setFocusItemsView();
		return true;
	}
	if (focuswidget == ui->tableView) {
		setFocusFolderTree();
		return true;
	}
	if (focuswidget == ui->thumbnailView) {
		setFocusFolderTree();
		return true;
	}
	if (focuswidget == ui->lineEdit_address) {
		setFocusFolderTree();
		return true;
	}
	return false;
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
	if (event->type() == QEvent::KeyPress) {
		if (QApplication::activeModalWidget()) {
			// thru
		} else {
			QKeyEvent *e = (QKeyEvent *)event;
			const int key = e->key();
			const bool alt = (e->modifiers() & Qt::AltModifier);
			const bool ctrl = (e->modifiers() & Qt::ControlModifier);
			const bool shift = (e->modifiers() & Qt::ShiftModifier);
			const bool enter = (key == Qt::Key_Enter || key == Qt::Key_Return);

			// qDebug() << Q_FUNC_INFO << key;

			QWidget *focuswidget = QApplication::focusWidget();
			const bool viewFocused = focuswidget == ui->treeView || focuswidget == ui->tableView || focuswidget == ui->thumbnailView;

			if ((key == Qt::Key_D && e->modifiers() == Qt::AltModifier)
					|| (key == Qt::Key_L && e->modifiers() == Qt::ControlModifier)) {
				setAddressBarVisible(true);
				return true;
			}

			auto AppendCharToFilterText = [&](){
				FilterTarget target;
				if (watched == ui->treeView) {
					target = FilterTarget::FolderTreeSearch;
				} else if (watched == ui->tableView) {
					target = FilterTarget::FileListViewSearch;
				} else if (watched == ui->thumbnailView) {
					target = FilterTarget::ThumbnailViewSearch;
				} else {
					return false;
				}
				QString text = e->text();
				return !(alt || ctrl) && appendCharToFilterText(text, target);
			};

			if (enter && shift) {
				setFocusItemsView();
				return true;
			}
			
			switch (key) {
			case Qt::Key_Tab:
			case Qt::Key_Backtab:
				if (handleTabKeyPressed()) {
					return true;
				}
				break;
			case Qt::Key_Backspace:
				if (shift) {
					setFocusFolderTree();
					return true;
				}
				break;
			case Qt::Key_Escape:
				if (focuswidget == ui->treeView) {
					auto *item = ui->treeView->currentItem();
					clearAllFilters();
					ui->treeView->setCurrentItem(item);
				} else if (focuswidget == ui->tableView) {
					auto path = ui->tableView->currentPath();
					clearAllFilters();
					ui->tableView->setPath(path);
				} else if (focuswidget == ui->thumbnailView) {
					auto path = ui->thumbnailView->currentPath();
					clearAllFilters();
					ui->thumbnailView->setPath(path);
				} else if (focuswidget == ui->lineEdit_address) {
					setAddressBarVisible(false);
					setFocusFolderTree();
					break;
				} else {
					setAddressBarVisible(false);
				}
				return true;
			case Qt::Key_Home:
				if (focuswidget == ui->tableView) {
					// ui->tableView->scrollToTop();
					ui->tableView->setCurrentTop();
					return true;
				}
				break;
			case Qt::Key_End:
				if (focuswidget == ui->tableView) {
					// ui->tableView->scrollToBottom();
					ui->tableView->setCurrentBottom();
					return true;
				}
				break;
			}

			if (QString text = e->text(); !text.isEmpty()) {
				if (viewFocused && !(e->modifiers() & (Qt::AltModifier | Qt::ControlModifier | Qt::MetaModifier))) {
					if (text == "[") {
						on_action_view_detailed_triggered();
						return true;
					}
					if (text == "]") {
						on_action_view_thumbnails_triggered();
						return true;
					}
				}
				if (text == "\\") {
					if (focuswidget != ui->lineEdit_address) {
						setHiddenFilesVisible(!hiddenFilesVisible());
						return true;
					}
				}
			}
			if (focuswidget == ui->treeView) {
				if (ctrl) {
					//
				} else {
					if (e->key() == '*') {
						updateCurrentFolder();
						return true; // suppress tree expansion
					}
					if (e->key() == Qt::Key_Left || enter) {
						if (ui->treeView->currentItem() == m->my_computer_item) {
							if (ui->treeView->isExpanded(m->my_computer_item)) {
								return true; // suppress tree collapse
							}
						}
					}
				}
			}
			if (viewFocused) {
				if (!ctrl) {
					if (AppendCharToFilterText()) {
						return true;
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
	// qDebug() << Q_FUNC_INFO << key;
	if (QApplication::activeModalWidget()) {
		// nop:
	} else {
		switch (key) {
		case Qt::Key_Tab:
		case Qt::Key_Backtab:
			if (handleTabKeyPressed()) {
				return;
			}
			break;
		case Qt::Key_Enter:
		case Qt::Key_Return:
			if (focuswidget == ui->treeView) {
				if (isIncrementalSearching()) {
					QString loc = locationInfo(ui->treeView->currentItem()).location;
					clearAllFilters();
					openDir(loc);
				}
				setFocusItemsView();
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
		case Qt::Key_Backspace:
			if (focuswidget == ui->treeView || focuswidget == ui->tableView || focuswidget == ui->thumbnailView) {
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
			if (event->modifiers() == Qt::AltModifier) {
				setAddressBarVisible(true);
				return;
			}
			break;
		case Qt::Key_L:
			if (event->modifiers() == Qt::ControlModifier) {
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
		// case Qt::Key_Period:
		// 	if (focuswidget == ui->lineEdit_address) {
		// 		return;
		// 	}
		// 	setHiddenFilesVisible(!hiddenFilesVisible());
		// 	return;
		case Qt::Key_Right:
			if (focuswidget == ui->treeView) {
				if (!isIncrementalSearching()) {
					updateCurrentFolder();
					return;
				}
			}
			break;
		}
	}
}

void MainWindow::setFocusItemsView()
{
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
}

QImage MainWindow::queryThumbnail(FileInfo2 const &info)
{
	if (info.isdir) {
		return global->folder_icon;
	}

	auto task = m->thumbnail_loader.query(info.path, info.modified, info.size);
	if (task) {
		return task->image;
	}
	return {};
}

void MainWindow::thumbnailReady(std::shared_ptr<ThumbnailLoader::Entiry> entity)
{
	if (!m->thumbnail_loader.isCurrent(entity) || !m->thumbnail_paths.count(entity->path)) return;
	ui->thumbnailView->updateThumbnail(entity->path, entity->image);
}

void MainWindow::setHiddenFilesVisible(bool f)
{
	const QString previous_filter = getIncrementalSearchText();
	const bool tree_search = filtertarget() == FilterTarget::FolderTreeSearch;
	ItemIdList selected = ui->treeView->currentItem() ? ui->treeView->currentItem()->iidl() : ItemIdList{};
	{
		QScopedValueRollback<bool> suppress(m->suppress_current_changed, true);
		QSignalBlocker block(ui->treeView);
		// Clearing a tree filter restores the pre-search selection and expansion.
		// Keep the search result's identity before any subtree can be replaced.
		clearAllFilters();
		if (selected.empty() && ui->treeView->currentItem()) selected = ui->treeView->currentItem()->iidl();
		const auto expanded = ui->treeView->expandedItemIds();
		auto *item = selected.empty() ? nullptr : ui->treeView->itemFromIidl(selected);
		m->hidden_files_visible = f;
		if (!f) {
			// Rebuild the parent of the outermost hidden ancestor so that the
			// selection cannot remain inside a folder removed by this toggle.
			for (auto *ancestor = item; ancestor; ancestor = ancestor->parent()) {
				Kind kind = static_cast<Kind>(ancestor->data(0, KindRole).toInt());
				if (kind != Kind::Directory && kind != Kind::SubDirectory && kind != Kind::NotPermittedDirectory) continue;
				QString path = ancestor->data(0, PathRole).toString();
				if ((ancestor->text().startsWith('.') || (!path.isEmpty() && QFileInfo(path).isHidden()))
					&& ancestor->parent() && ui->treeView->indexFromItem(ancestor->parent()).isValid()) {
					item = ancestor->parent();
					selected = item->iidl();
				}
			}
		}
		if (item && static_cast<Kind>(item->data(0, KindRole).toInt()) != Kind::ChromeBookmark
			&& location(item) != (QS)prefix_mycomputer) {
			fetchSubFolders(item, false);
			auto RestoreExpansion = [&](auto self, FolderTreeItem *node) -> void {
				bool was_expanded = expanded.find(node->iidl()) != expanded.end();
				if (was_expanded) fetchSubFolders(node);
				ui->treeView->setExpanded(node, was_expanded);
				for (int row = 0; row < node->childCount(); ++row) self(self, node->child(row));
			};
			RestoreExpansion(RestoreExpansion, item);
		}
		ui->treeView->setCurrentItem(selected.empty() ? nullptr : ui->treeView->itemFromIidl(selected));
		if (!tree_search && !previous_filter.isEmpty()) setIncrementalSearchText(previous_filter);
		setCurrentIIDL(selected);
		ui->lineEdit_address->setText(locationInfo(ui->treeView->currentItem()).location);
	}
	m->thumbnail_loader.clearRequests();
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
		if (i == 0) { // 先頭のドットは拡張子ではない
			// nop
		} else if (i > 0) {
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
		// return iconprov.icon(QFileIconProvider::Folder);
		return QIcon(QPixmap::fromImage(global->folder_icon));
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

QString MainWindow::location(FolderTreeItem const *item) const
{
	QString loc;
	if (item) {
		loc = item->data(0, PathRole).toString();
		if (loc.isEmpty()) {
			ItemIdList iidl = item->data(0, IidlRole).value<ItemIdList>();
			if (iidl.type() == ItemIdList::Type::PATH) {
				loc = iidl.path();
			} else if (iidl.type() == ItemIdList::Type::WIN_SHELL_ITEMIDLIST) {
				loc = (QS)prefix_itemidlist;
				for (int i = 0; i < iidl.size(); i++) {
					loc += QString::asprintf("%02x", (uint8_t)iidl.data()[i]);
				}
			}
		}
	}
	return loc;
}

QString MainWindow::currentLocation() const
{
	auto item = ui->treeView->currentItem();
	return location(item);
}

MainWindow::LocationInfo MainWindow::locationInfo(QString loc) const
{
	if (loc.isEmpty()) return {};
	
	LocationInfo ret;
	ret.location = loc;
	
	if (loc == (QS)prefix_mycomputer) {
		ret.kind = LocationInfo::Computer;
		ret.prefix = prefix_mycomputer;
		return ret;
	}
	if (loc.startsWith((misc::str)prefix_bookmark)) {
		ret.kind = LocationInfo::BookmarkFolder;
		ret.prefix = prefix_bookmark;
		return ret;
	}
	if (loc.startsWith((misc::str)prefix_itemidlist)) {
#ifdef Q_OS_WIN
		if (!splitShellItemIdList(ItemIdList(loc))) return {};
		ret.kind = LocationInfo::ShellFolder;
		ret.prefix = prefix_itemidlist;
		return ret;
#else
		return {};
#endif
	}
	
#ifdef Q_OS_WIN
	loc = loc.replace('/', '\\');
#endif
	QFileInfo info(loc);
	if (info.isFile()) {
		ret.kind = LocationInfo::File;
		return ret;
	}
	if (info.isDir()) {
		ret.kind = LocationInfo::Directory;
		return ret;
	}
	
	return {};
}

MainWindow::LocationInfo MainWindow::locationInfo(FolderTreeItem *item) const
{
	if (!item) return {};
	LocationInfo info = locationInfo(location(item));
	if (static_cast<Kind>(item->data(0, KindRole).toInt()) == Kind::ChromeBookmark) {
		info.kind = LocationInfo::BookmarkFolder;
	}
	return info;
}

bool MainWindow::isDir(FolderTreeItem *item) const
{
	LocationInfo::Kind kind = locationInfo(item).kind;
	return kind == LocationInfo::Directory || kind == LocationInfo::ShellFolder
		|| kind == LocationInfo::BookmarkFolder || kind == LocationInfo::Computer;
}

void MainWindow::on_treeView_currentItemChanged(FolderTreeItem *current, FolderTreeItem *previous)
{
	// qDebug() << Q_FUNC_INFO;

	(void)current;
	(void)previous;

	if (m->suppress_current_changed) return;

	m->thumbnail_loader.clearRequests();
	
	{
		QSignalBlocker block(ui->treeView);
		ui->treeView->scrollTo(ui->treeView->indexFromItem(current), QTreeView::EnsureVisible);
	}
	if (isIncrementalSearching() && filtertarget() != FilterTarget::FolderTreeSearch) clearAllFilters();

	{
		auto item = ui->treeView->currentItem();
		if (item) {
			ItemIdList iidl = item->data(0, IidlRole).value<ItemIdList>();
			setCurrentIIDL(iidl);
		}
	}

	LocationInfo info = locationInfo(ui->treeView->currentItem());
#ifdef Q_OS_WIN
	if (current && current->data(0, IidlRole).value<ItemIdList>() == FileItem::getNetwork().idlist()) {
		m->network_discovery_thread.refresh(); // ネットワークを選択したら最新化を促す
	}
#endif
	updateFileView();
	ui->lineEdit_address->setText(info.location);
}

#ifdef Q_OS_WIN
FolderTreeItem *MainWindow::openDirWindows(ItemIdList const &iidl)
{
	auto list = splitShellItemIdList(iidl);
	if (!list) return nullptr;
	// The Desktop PIDL consists only of a valid two-byte terminator.
	if (list->empty()) return ui->treeView->itemFromIidl(iidl);

	FolderTreeItem *item = nullptr;
	for (ItemIdList const &part : *list) {
		FolderTreeItem *next = ui->treeView->itemFromIidl(part);
		if (!next && item) {
			// Shell parsing and enumeration can describe the same folder with
			// different PIDL metadata. Compare identities within its parent.
			IShellFolder *desktop = global->shapi->getDesktopShellFolder();
			if (desktop) {
				for (int row = 0; row < item->childCount(); ++row) {
					FolderTreeItem *child = item->child(row);
					ItemIdList child_iidl = child->iidl();
					if (!splitShellItemIdList(child_iidl)) continue;
					HRESULT result = desktop->CompareIDs(SHCIDS_CANONICALONLY,
						reinterpret_cast<ITEMIDLIST const *>(part.data()),
						reinterpret_cast<ITEMIDLIST const *>(child_iidl.data()));
					if (SUCCEEDED(result) && HRESULT_CODE(result) == 0) {
						next = child;
						break;
					}
				}
			}
		}
		if (!next) return nullptr;
		item = next;
		fetchSubFolders(item);
		ui->treeView->setExpanded(item, true);
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
	auto fail = [&](){ setStatusBarText(tr("Cannot open this location.")); };
	if (iidl.empty()) {
		fail();
		return;
	}
	if (iidl.type() == ItemIdList::Type::PATH) {
		QString path = iidl.path();
		if (path.startsWith((misc::str)prefix_bookmark) || path == (QS)prefix_mycomputer) {
			FolderTreeItem *item = path == (QS)prefix_mycomputer ? m->my_computer_item
				: findBookmarkTreeItem(path, m->bookmarks_root_item);
			if (!item) {
				fail();
				return;
			}
			{
				QScopedValueRollback<bool> suppress(m->suppress_current_changed, true);
				QSignalBlocker block(ui->treeView);
				clearAllFilters();
				ui->treeView->setCurrentItem(item);
				ui->treeView->setExpanded(item, true);
				ui->treeView->scrollTo(ui->treeView->indexFromItem(item), QTreeView::EnsureVisible);
			}
			on_treeView_currentItemChanged(item, nullptr);
			return;
		}
	}
	ItemIdList destination = iidl;
#ifdef Q_OS_WIN
	if (destination.type() == ItemIdList::Type::PATH) {
		destination = ItemIdList(destination.path());
	}
	// Reject truncated PIDLs before any Shell API reads them.
	if (!splitShellItemIdList(destination) || !FileItem::fromITEMIDLIST(destination)) {
		fail();
		return;
	}
#else
	if (!QFileInfo(destination.path()).isDir()) {
		fail();
		return;
	}
#endif
	const QString previous_filter = getIncrementalSearchText();
	FolderTreeItem *previous_item = ui->treeView->currentItem();
	struct Selection {
		QAbstractItemView *view;
		int row;
		int column;
		std::vector<int> selected_rows;
		explicit Selection(QAbstractItemView *view) : view(view), row(view->currentIndex().row()), column(view->currentIndex().column())
		{
			if (view->selectionModel()) {
				for (auto const &index : view->selectionModel()->selectedRows()) selected_rows.push_back(index.row());
			}
		}
		void restore() const
		{
			if (!view->selectionModel()) return;
			view->selectionModel()->clearSelection();
			for (int selected_row : selected_rows) {
				view->selectionModel()->select(view->model()->index(selected_row, 0), QItemSelectionModel::Select | QItemSelectionModel::Rows);
			}
			view->selectionModel()->setCurrentIndex(view->model()->index(row, column), QItemSelectionModel::NoUpdate);
		}
	};
	Selection table_selection(ui->tableView), thumbnail_selection(ui->thumbnailView);
	FolderTreeItem *item = nullptr;
	{
		QScopedValueRollback<bool> suppress(m->suppress_current_changed, true);
		QSignalBlocker block(ui->treeView);
		clearAllFilters();
#ifdef Q_OS_WIN
		item = openDirWindows(destination);
#else
		item = openDirPosix(destination);
#endif
		if (!item) {
			setIncrementalSearchText(previous_filter);
			ui->treeView->setCurrentItem(previous_item);
			table_selection.restore();
			thumbnail_selection.restore();
			updateStatusBarText();
			fail();
			return;
		}
		setCurrentIIDL(destination);
		ui->treeView->setCurrentItem(item);
		ui->treeView->setExpanded(item, true);
		// scrollTo can expand ancestors; keep their expansion handlers from
		// replacing the destination selection while committing navigation.
		ui->treeView->scrollTo(ui->treeView->indexFromItem(item), QTreeView::EnsureVisible);
	}
	// Also refresh when navigating to the already selected directory.
	on_treeView_currentItemChanged(item, nullptr);
}

bool MainWindow::isAddressBarVisible() const
{
	return ui->frame_tool_bar->isVisible();
}

void MainWindow::setAddressBarVisible(bool visible)
{
	if (visible) {
		QWidget *focuswidget = QWidget::focusWidget();
		if (focuswidget == ui->treeView || focuswidget == ui->tableView || focuswidget == ui->thumbnailView) {
			m->focus_widget = focuswidget;
		}
		ui->frame_tool_bar->setVisible(true);
		ui->lineEdit_address->setFocus();
		ui->lineEdit_address->selectAll();
	} else if (isAddressBarVisible()) {
		ui->frame_tool_bar->setVisible(false);
		if (m->focus_widget) {
			m->focus_widget->setFocus();
			m->focus_widget = nullptr;
		} else {
			setFocusFolderTree();
		}
	}
}

void MainWindow::toggleBookbarkBarVisible()
{
	ui->frame_bookmark_bar->setVisible(!ui->frame_bookmark_bar->isVisible());
}

void MainWindow::makeBookmarkMap(QString const &path, QJsonValue v, int depth, FolderTreeItem *parent)
{
	if (!v.isObject() || !parent) return;
	QJsonObject object = v.toObject();
	QString name = object.value("name").toString();
	if (object.value("children").isArray()) {
		QString id = object.value("id").toVariant().toString();
		if (id.isEmpty()) id = QString("folder-%1").arg(parent->childCount());
		QString base = path + (path.endsWith('/') ? QString() : QString("/"))
			+ QString::fromLatin1(QUrl::toPercentEncoding(id));
		QString childpath = base;
		for (int duplicate = 1; m->bookmark_items.count(childpath); ++duplicate) {
			childpath = base + '-' + QString::number(duplicate);
		}
		m->bookmark_items[path].push_back(BookmarkInfo(name, childpath, true));
		m->bookmark_items.try_emplace(childpath); // Empty folders are real locations too.
		auto *child = new_FolderTreeItem();
		child->setText(0, name);
		child->setData(0, KindRole, static_cast<int>(Kind::ChromeBookmark));
		child->setData(0, PathRole, childpath);
		child->setData(0, IidlRole, QVariant::fromValue(ItemIdList(childpath)));
		parent->addChild(child);
		for (QJsonValue const &entry : object.value("children").toArray()) {
			makeBookmarkMap(childpath, entry, depth + 1, child);
		}
	} else if (object.value("url").isString()) {
		m->bookmark_items[path].push_back(BookmarkInfo(name, object.value("url").toString()));
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

void MainWindow::fetchBookmarks(QString const &sourcePath)
{
	if (!sourcePath.isEmpty()) m->bookmark_file_path = sourcePath;
	if (!m->bookmarks_root_item) return;
	QString path = m->bookmark_file_path;
	if (path.isEmpty()) {
#ifdef Q_OS_MAC
		path = QDir::homePath() / "Library/Application Support/Google/Chrome/Default/Bookmarks";
#elif defined(Q_OS_WIN)
		path = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
			/ "Google/Chrome/User Data/Default/Bookmarks";
#else
		path = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
			/ "google-chrome/Default/Bookmarks";
#endif
	}
	FolderTreeItem *current = ui->treeView->currentItem();
	QString selected = current && static_cast<Kind>(current->data(0, KindRole).toInt()) == Kind::ChromeBookmark
		? location(current) : QString();
	{
		QScopedValueRollback<bool> suppress(m->suppress_current_changed, true);
		QSignalBlocker block(ui->treeView);
		clearAllFilters();
		while (m->bookmarks_root_item->childCount() > 0) {
			delete m->bookmarks_root_item->takeChild(m->bookmarks_root_item->childCount() - 1);
		}
		m->bookmark_items.clear();
		m->bookmark_items.try_emplace((QS)prefix_bookmark);
		QFile file(path);
		if (file.open(QFile::ReadOnly)) {
			QJsonObject roots = QJsonDocument::fromJson(file.readAll()).object().value("roots").toObject();
			for (QString const &key : roots.keys()) {
				makeBookmarkMap((QS)prefix_bookmark, roots.value(key), 0, m->bookmarks_root_item);
			}
		}
		ui->treeView->setExpanded(m->bookmarks_root_item, true);
		if (!selected.isEmpty()) {
			current = findBookmarkTreeItem(selected, m->bookmarks_root_item);
			if (!current) current = m->bookmarks_root_item;
			ui->treeView->setCurrentItem(current);
			ui->treeView->scrollTo(ui->treeView->indexFromItem(current), QTreeView::EnsureVisible);
		}
	}
	if (!selected.isEmpty() && !m->suppress_current_changed) on_treeView_currentItemChanged(current, nullptr);
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
}

