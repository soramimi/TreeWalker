#include "FileItemModel.h"
#include "ApplicationGlobal.h"
#include "MainWindow.h"
#include <algorithm>
#include <numeric>

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
	for (auto &item : items_) {
		prepareItem(&item, kind);
	}
	sorted_indices_valid_ = false;
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
	if (!sorted_indices_valid_) {
		sorted_indices_.resize(items_.size());
		std::iota(sorted_indices_.begin(), sorted_indices_.end(), size_t{0});
		std::stable_sort(sorted_indices_.begin(), sorted_indices_.end(), [this](size_t left, size_t right) {
			auto const &l = items_[left];
			auto const &r = items_[right];
			if (l.info.isdir != r.info.isdir) return l.info.isdir;

			int comparison = 0;

			if (kind_ == Kind::ChromeBookmark) {
				qDebug() << l.path << r.path;
				switch (sort_column_) {
				case SortColumn::URL:
					comparison = QString::compare(l.path, r.path, Qt::CaseInsensitive) < 0;
					break;
				}
				if (comparison == 0) comparison = QString::compare(l.name, r.name, Qt::CaseInsensitive);
			} else {
				switch (sort_column_) {
				case SortColumn::Name:
					break;
				case SortColumn::Size:
					comparison = (l.size > r.size) - (l.size < r.size);
					break;
				case SortColumn::Type:
					comparison = QString::compare(l.type, r.type, Qt::CaseInsensitive);
					break;
				case SortColumn::Modified:
					comparison = (l.modified > r.modified) - (l.modified < r.modified);
					break;
				}
				if (comparison == 0) comparison = QString::compare(l.name, r.name, Qt::CaseInsensitive);
				if (comparison == 0) comparison = QString::compare(l.path, r.path);
			}
			return sort_order_ == Qt::AscendingOrder ? comparison < 0 : comparison > 0;
		});
		sorted_indices_valid_ = true;
	}
	filtered_indices_.clear();
	if (isFiltered()) {
		filtered_indices_.reserve(sorted_indices_.size());
		for (auto index : sorted_indices_) {
			Item const &item = items_[index];
			if (match_(item.search_text[0], filter_)) {
				filtered_indices_.push_back(index);
			}
		}
	} else {
		filtered_indices_ = sorted_indices_;
	}
	indices_valid_ = true;
}

int FileItemModel::count() const
{
	if (!indices_valid_) {
		const_cast<FileItemModel *>(this)->updateIndices();
	}
	return filtered_indices_.size();
}

int FileItemModel::unfilteredCount() const
{
	return items_.size();
}

int FileItemModel::unfilteredRow(int row) const
{
	if (row < 0 || row >= count()) return -1;
	if (!isFiltered()) return row;
	return std::find(sorted_indices_.begin(), sorted_indices_.end(), filtered_indices_[row]) - sorted_indices_.begin();
}

FileItemModel::Item *FileItemModel::item(int row)
{
	int n = count();
	if (row >= 0 && row < n) {
		size_t index = filtered_indices_[row];
		return &items_[index];
	}
	return nullptr;
}

FileItemModel::Item const *FileItemModel::item(int row) const
{
	return const_cast<FileItemModel *>(this)->item(row);
}

void FileItemModel::clearItems()
{
	items_.clear();
	sorted_indices_valid_ = false;
	indices_valid_ = false;
}

void FileItemModel::addItem(Item &&item)
{
	prepareItem(&item, kind_);
	items_.push_back(std::move(item));
	sorted_indices_valid_ = false;
	indices_valid_ = false;
}

void FileItemModel::replaceItems(Kind kind, std::vector<Item> items, QString const &filter_text)
{
	for (auto &item : items) {
		prepareItem(&item, kind);
	}
	auto filter = filter_text.isEmpty() ? IncrementalSearchFilter{} : global->incremental_search->makeFilter(filter_text.toStdString());
	beginResetModel();
	items_ = std::move(items);
	kind_ = kind;
	filter_ = std::move(filter);
	sorted_indices_valid_ = false;
	indices_valid_ = false;
	endResetModel();
}

void FileItemModel::prepareItem(Item *item, Kind kind)
{
	for (int column = 0; column < (kind == Kind::ChromeBookmark ? 2 : 4); ++column) {
		item->search_text[column] = fromQString(item->displayText(kind, column));
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
		filter_ = filter_text.isEmpty() ? IncrementalSearchFilter{} : global->incremental_search->makeFilter(filter_text.toStdString());
		indices_valid_ = false;
	}
	endResetModel();
}

void FileItemModel::sort(int column, Qt::SortOrder order)
{
	if (kind_ == Kind::ChromeBookmark || column < 0 || column >= columnCount()) return;
	auto sort_column = static_cast<SortColumn>(column);
	if (sort_column_ == sort_column && sort_order_ == order) return;

	count();
	emit layoutAboutToBeChanged({}, QAbstractItemModel::VerticalSortHint);
	auto previous = persistentIndexList();
	std::vector<size_t> item_indices;
	item_indices.reserve(previous.size());
	for (auto const &index : previous) item_indices.push_back(filtered_indices_[index.row()]);

	sort_column_ = sort_column;
	sort_order_ = order;
	sorted_indices_valid_ = false;
	indices_valid_ = false;
	updateIndices();

	if (!previous.isEmpty()) {
		std::vector<int> rows(items_.size());
		for (int row = 0; row < count(); ++row) rows[filtered_indices_[row]] = row;
		QModelIndexList updated;
		updated.reserve(previous.size());
		for (qsizetype i = 0; i < previous.size(); ++i) {
			updated.push_back(index(rows[item_indices[i]], previous[i].column()));
		}
		changePersistentIndexList(previous, updated);
	}
	emit layoutChanged({}, QAbstractItemModel::VerticalSortHint);
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
				FileItemModel::Item const *item = this->item(row);
				if (global && item->info.isdir) {
					return QIcon(QPixmap::fromImage(global->folder_icon));
				}
				if (kind_ == Kind::ChromeBookmark) {
					return QIcon(QPixmap::fromImage(global->bookmark_icon));
				}
				if (!item->icon_.isNull()) return item->icon_;
				if (global && global->mainwindow) return global->mainwindow->getIcon(item->info);
				return item->info.icon;
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




