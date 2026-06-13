#ifndef FILEITEMMODEL_H
#define FILEITEMMODEL_H

#include "AbstractFileSystemProvider.h"

#include <QAbstractItemModel>
#include <QDateTime>
#include <QIcon>

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
	QList<Item> items;
	Kind kind_ = Kind::File;
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
private:
	QWidget *QAbstractItemModel;
};

#endif // FILEITEMMODEL_H
