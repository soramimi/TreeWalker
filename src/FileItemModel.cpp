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

int FileItemModel::rowCount(const QModelIndex &parent) const
{
	return items.size();
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
	if (row >= 0 && row < items.size()) {
		int col = index.column();
		QString text;
		switch (role) {
		case Qt::DisplayRole:
			if (kind_ == Kind::ChromeBookmark) {
				switch (col) {
				case 0:
					return items[row].name;
				case 1:
					return items[row].path;
				}
			} else {
				switch (col) {
				case 0:
					text = items[row].name;
					break;
				case 1:
					{
						text = (items[row].size == -1) ? QString() : QString::number(items[row].size);
						int i = text.size();
						while (i > 3) {
							i -= 3;
							text.insert(i, ',');
						}
					}
					break;
				case 2:
					text = items[row].type;
					break;
				case 3:
					text = modifiedText(items[row].modified);
					break;
				}
			}
			return text;
		case Qt::DecorationRole:
			if (col == 0) {
				return global->mainwindow->getIcon(items[row].info);
				// return items[row].icon;
			}
			break;
		case Qt::SizeHintRole:
			return QSize(128, 128);
		case PathRole:
			return items[row].path;
		case UrlRole:
			return items[row].path;
			break;
		case IidlRole:
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




