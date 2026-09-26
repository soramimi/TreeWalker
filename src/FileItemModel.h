#ifndef FILEITEMMODEL_H
#define FILEITEMMODEL_H

#include "AbstractFileSystemProvider.h"

#include <QAbstractItemModel>
#include <QDateTime>
#include <QIcon>

#include <subprojects/IncrementalSearchPlugin/src/IncrementalSearch.h>

enum ItemRole {
	KindRole = Qt::UserRole,
	NameRole,
	PathRole,
	UrlRole,
	IidlRole,
};

enum class Kind {
	File,
	Directory,
	SubDirectory,
	NotPermittedDirectory,
	Placeholder,
	ChromeBookmark,
};

class FileItemModel : public QAbstractItemModel {
	Q_OBJECT
public:
	struct Item {
		FileInfo2 info;
		QString name;
		QString path;
		QIcon icon_;
		qint64 size = -1;
		QString type;
		QDateTime modified;
		bool hidden = false;
		QIcon icon() const
		{
			return icon_;
		}
	};
	Kind kind_ = Kind::File;
private:
	std::vector<Item> items_;
	mutable std::vector<size_t> indices_;
	IncrementalSearchFilter filter_;
	void updateIndices();
public:
	int count() const;
	int unfilteredRow(int row);
	FileItemModel::Item *item(int row);
	FileItemModel::Item const *item(int row) const;
	void clearItems();
	void addItem(FileItemModel::Item &&item);
public:
	mutable std::optional<int> filtered_items_;
	bool isFiltered() const;
	void setFilterText(QString const &filter_text);
public:
	FileItemModel(QWidget *parent = nullptr);
	virtual ~FileItemModel() = default;
	void setKind(Kind kind);
	QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const;
	QModelIndex parent(const QModelIndex &child) const;
	int rowCount(const QModelIndex &parent = QModelIndex()) const;
	int columnCount(const QModelIndex &parent = QModelIndex()) const;
	QVariant data(const QModelIndex &index, int role) const;
	QVariant headerData(int section, Qt::Orientation orientation, int role) const;
	FileInfo2 const *fileinfo(const QModelIndex &index) const;
	using QAbstractItemModel::beginResetModel;
	using QAbstractItemModel::endResetModel;
	IncrementalSearchFilter const &filter() const;
};

#endif // FILEITEMMODEL_H
