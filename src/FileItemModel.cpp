#include "FileItemModel.h"
#include "ApplicationGlobal.h"
#include "MainWindow.h"

FileItemModel::FileItemModel(QWidget *parent, MatchFunction match)
	: QAbstractItemModel(parent)
	, match_(match ? std::move(match) : MatchFunction([](auto const &text, auto const &filter) {
		return global->incremental_search->match(text, filter);
	}))
{
}

void FileItemModel::setKind(Kind kind)
{
	if (kind_ == kind) return;
	kind_ = kind;
	for (auto &item : items_) prepareItem(&item, kind);
	indices_.clear();
	indices_valid_ = false;
}

QModelIndex FileItemModel::index(int row, int column, const QModelIndex &parent) const
{
	return hasIndex(row, column, parent) ? createIndex(row, column) : QModelIndex();
}

QModelIndex FileItemModel::parent(const QModelIndex &child) const
{
	return QModelIndex();
}

void FileItemModel::updateIndices()
{
	indices_.clear();
	if (isFiltered()) {
		indices_.reserve(items_.size());
		for (size_t i = 0; i < items_.size(); i++) {
			Item const &item = items_[i];
			if (match_(item.search_text[0], filter_)) {
				indices_.push_back(i);
			}
		}
	} else {
		indices_.resize(items_.size());
		std::iota(indices_.begin(), indices_.end(), 0);
	}
	indices_valid_ = true;
}

int FileItemModel::count() const
{
	if (!indices_valid_) {
		const_cast<FileItemModel *>(this)->updateIndices();
	}
	return indices_.size();
}

int FileItemModel::unfilteredCount() const
{
	return items_.size();
}

int FileItemModel::unfilteredRow(int row) const
{
	if (row >= 0) {
		size_t cnt = indices_.size();
		if (isFiltered()) {
			cnt = count();
			if (row < cnt) {
				return indices_[row];
			}
		} else if (row < items_.size()) {
			return row;
		}
	}
	return -1;
}

FileItemModel::Item *FileItemModel::item(int row)
{
	count();
	if (row < 0 || row >= indices_.size()) return nullptr;
	size_t index = indices_[row];
	return &items_[index];
}

FileItemModel::Item const *FileItemModel::item(int row) const
{
	return const_cast<FileItemModel *>(this)->item(row);
}

void FileItemModel::clearItems()
{
	items_.clear();
	indices_.clear();
	indices_valid_ = false;
}

void FileItemModel::addItem(Item &&item)
{
	prepareItem(&item, kind_);
	items_.push_back(std::move(item));
	indices_.clear();
	indices_valid_ = false;
}

void FileItemModel::replaceItems(Kind kind, std::vector<Item> items, QString const &filter_text)
{
	for (auto &item : items) prepareItem(&item, kind);
	auto filter = global->incremental_search->makeFilter(filter_text.toStdString());
	beginResetModel();
	items_ = std::move(items);
	kind_ = kind;
	filter_ = std::move(filter);
	indices_.clear();
	indices_valid_ = false;
	endResetModel();
}

void FileItemModel::prepareItem(Item *item, Kind kind)
{
	for (int column = 0; column < (kind == Kind::ChromeBookmark ? 2 : 4); ++column) {
		item->search_text[column] = global->incremental_search->prepare(fromQString(item->displayText(kind, column)));
	}
}

incrementalsearch::Result FileItemModel::matchText(QModelIndex const &index) const
{
	if (!index.isValid() || index.model() != this || index.column() >= columnCount() || index.row() >= count()) return {};
	return match_(item(index.row())->search_text[index.column()], filter_);
}

IncrementalSearchFilter const &FileItemModel::filter() const
{
	return filter_;
}

bool FileItemModel::isFiltered() const
{
	return (bool)filter();
}

void FileItemModel::setFilterText(const QString &filter_text)
{
	beginResetModel();
	{
		filter_ = global->incremental_search->makeFilter(filter_text.toStdString());
		indices_.clear();
		indices_valid_ = false;
	}
	endResetModel();
}

int FileItemModel::rowCount(const QModelIndex &parent) const
{
	return parent.isValid() ? 0 : count();
}

int FileItemModel::columnCount(const QModelIndex &parent) const
{
	if (parent.isValid()) return 0;
	return kind_ == Kind::ChromeBookmark ? 2 : 4;
}

static QString modifiedText(QDateTime const &dt)
{
	if (dt.isValid()) {
		int year = dt.date().year();
		int month = dt.date().month();
		int day = dt.date().day();
		int hour = dt.time().hour();
		int minute = dt.time().minute();
		int second = dt.time().second();
		return QString::asprintf("%04u-%02u-%02u %02u:%02u:%02u", year, month, day, hour, minute, second);
	}
	return QString();
}

QString FileItemModel::Item::displayText(Kind kind, int column) const
{
	if (kind == Kind::ChromeBookmark) {
		return column == 0 ? name : info.isdir ? QString() : path;
	}
	switch (column) {
	case 0: return name;
	case 1: {
		QString text = size == -1 ? QString() : QString::number(size);
		for (int i = text.size() - 3; i > 0; i -= 3) text.insert(i, ',');
		return text;
	}
	case 2: return type;
	case 3: return modifiedText(modified);
	}
	return {};
}

QVariant FileItemModel::data(const QModelIndex &index, int role) const
{
	if (!index.isValid() || index.model() != this || index.column() >= columnCount()) return {};
	int row = index.row();
	if (row >= 0 && row < count()) {
		int col = index.column();
		QString text;
		switch (role) {
		case Qt::DisplayRole:
			return item(row)->displayText(kind_, col);
		case Qt::DecorationRole:
			if (col == 0) {
				FileItemModel::Item const *item= this->item(row);
				if (!item->icon_.isNull()) return item->icon_;
				if (item && item->info.isdir) {
					return QIcon(QPixmap::fromImage(global->folder_icon));
				}
				return global->mainwindow->getIcon(item->info);
			}
			break;
		case Qt::SizeHintRole:
			return QSize(128, 128);
		case PathRole:
			return item(row)->path;
		case UrlRole:
			return item(row)->path;
			break;
		case IidlRole:
			return QVariant::fromValue(item(row)->info.iidl);
		case KindRole:
			if (item(row)->info.isdir) {
				bool bookmark = kind_ == Kind::ChromeBookmark || item(row)->path.startsWith(QString::fromLatin1(prefix_bookmark.data(), prefix_bookmark.size()));
				return (int)(bookmark ? Kind::ChromeBookmark : Kind::Directory);
			}
			return (int)Kind::File;
		}
	}
	return QVariant();
}

QVariant FileItemModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	if (role == Qt::DisplayRole && orientation == Qt::Horizontal) {
		if (kind_ == Kind::ChromeBookmark) {
			switch (section) {
			case 0:
				return tr("Name");
			case 1:
				return tr("URL");
			}
		} else {
			switch (section) {
			case 0:
				return tr("Name");
			case 1:
				return tr("Size");
			case 2:
				return tr("Type");
			case 3:
				return tr("Modified");
			}
		}
	}
	return QVariant();
}

const FileInfo2 *FileItemModel::fileinfo(const QModelIndex &index) const
{
	if (!index.isValid() || index.model() != this || index.column() >= columnCount()) return nullptr;
	int row = index.row();
	if (row >= 0 && row < count()) {
		return &item(row)->info;
	}
	return nullptr;
}




