#ifndef FOLDERTREEVIEW_H
#define FOLDERTREEVIEW_H

#include "ItemIdList.h"

#include <QTreeView>
#include <memory>

class FolderTreeModel;

class FolderTreeItem {
	friend class FolderTreeModel;
private:
	FolderTreeModel *model_ = nullptr;
	QString text_;
	ItemIdList iidl_;
	QMap<int, QVariant> data_map_;
	FolderTreeItem *parent_;
	std::vector<FolderTreeItem *> children_;
	mutable std::unordered_map<void *, size_t> children_cache_; // pointer to index in children vector
private:
	FolderTreeModel *model()
	{
		return model_;
	}
	void setModel(FolderTreeModel *model)
	{
		model_ = model;
	}
	std::vector<FolderTreeItem *> *children()
	{
		return &children_;
	}
	std::vector<FolderTreeItem *> const *children() const
	{
		return &children_;
	}

	QMap<int, QVariant> data_map()
	{
		return data_map_;
	}

	void clearChildrenCache();
	void updateChildrenCache();
public:
	FolderTreeItem *parent();
	FolderTreeItem *child(int i) const;

	void addChild(FolderTreeItem *child);
	FolderTreeItem *takeChild(int row);

	QString text() const;
	void setText(int column, QString const &text);

	void setData(int column, int role, QVariant const &data);
	QVariant data(int column, int role) const;

	int childCount() const { return children_.size(); }

	ItemIdList const &iidl() const
	{
		return iidl_;
	}
};

class FolderTreeView : public QTreeView {
	Q_OBJECT
private:
	struct Private;
	Private *m;
protected:
	void beginResetModel();
	void endResetModel();

public:
	explicit FolderTreeView(QWidget *parent = 0);
	~FolderTreeView();
	void clear();

	QModelIndex indexFromItem(FolderTreeItem *item) const;
	FolderTreeItem *itemFromIndex(const QModelIndex &index) const;

	FolderTreeItem *itemFromIidl(ItemIdList iidl) const;

	void addTopLevelItem(FolderTreeItem *item);

	FolderTreeItem *currentItem() const;
	void setCurrentItem(FolderTreeItem *item);

	using QTreeView::isExpanded;
	using QTreeView::setExpanded;
	bool isExpanded(FolderTreeItem *item) const;
	void setExpanded(FolderTreeItem *item, bool f);

protected slots:
	void currentChanged(const QModelIndex &current, const QModelIndex &previous);
private slots:
	void onExpanded(const QModelIndex &index);
signals:
	void currentItemChanged(FolderTreeItem *current, FolderTreeItem *previous);
	void expanded(FolderTreeItem *item);
};

#endif // FOLDERTREEVIEW_H
