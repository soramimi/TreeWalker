#include "FileItemModel.h"
#include "ApplicationGlobal.h"
#include "MainWindow.h"

FileItemModel::FileItemModel(QWidget *parent)
	: QAbstractItemModel(parent)
{
}

void FileItemModel::setKind(Kind kind)
{
	kind_ = kind;
}

QModelIndex FileItemModel::index(int row, int column, const QModelIndex &parent) const
{
	return createIndex(row, column);
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
			if (global->incremental_search->match(item.name.toStdString(), filter_)) {
				indices_.push_back(i);
			}
		}
	} else {
		indices_.resize(items_.size());
		std::iota(indices_.begin(), indices_.end(), 0);
	}
}

int FileItemModel::count() const
{
	if (indices_.empty()) {
		const_cast<FileItemModel *>(this)->updateIndices();
	}
	return indices_.size();
}

FileItemModel::Item *FileItemModel::item(int row)
{
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
}

void FileItemModel::addItem(Item &&item)
{
	items_.push_back(std::move(item));
	indices_.clear();
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
	}
	endResetModel();
}

int FileItemModel::rowCount(const QModelIndex &parent) const
{
	return count();
}

int FileItemModel::columnCount(const QModelIndex &parent) const
{
	return 4;
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

QVariant FileItemModel::data(const QModelIndex &index, int role) const
{
	int row = index.row();
	if (row >= 0 && row < count()) {
		int col = index.column();
		QString text;
		switch (role) {
		case Qt::DisplayRole:
			if (kind_ == Kind::ChromeBookmark) {
				switch (col) {
				case 0:
					return item(row)->name;
				case 1:
					return item(row)->path;
				}
			} else {
				switch (col) {
				case 0:
					text = item(row)->name;
					break;
				case 1:
					{
						text = (item(row)->size == -1) ? QString() : QString::number(item(row)->size);
						int i = text.size();
						while (i > 3) {
							i -= 3;
							text.insert(i, ',');
						}
					}
					break;
				case 2:
					text = item(row)->type;
					break;
				case 3:
					text = modifiedText(item(row)->modified);
					break;
				}
			}
			return text;
		case Qt::DecorationRole:
			if (col == 0) {
				return global->mainwindow->getIcon(item(row)->info);
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
			break;
		case KindRole:
			break;
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
	int row = index.row();
	if (row >= 0 && row < count()) {
		int col = index.column();
		return &item(row)->info;
	}
	return nullptr;
}




