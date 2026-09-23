#ifndef FOLDERTREEVIEW_H
#define FOLDERTREEVIEW_H

#include "ItemIdList.h"

#include <QTreeView>
#include <memory>

#include <subprojects/IncrementalSearchPlugin/src/IncrementalSearch.h>

class FolderTreeModel;

class FolderTreeItem {
	friend class FolderTreeModel;
	friend class FolderTreeView;
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

	void addChild(FolderTreeItem *child, int row = -1);
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
	friend class FolderTreeItemDelegate;
private:
	struct Private;
	Private *m;

	// IncrementalSearchFilter makeIncrementalSearchFilter() const;
	
	void _set_filter(const QString &filter_text);
	const IncrementalSearchFilter &filter() const;
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

	void setFilter(QString const &filter_text);
	
protected slots:
	void currentChanged(const QModelIndex &current, const QModelIndex &previous);
private slots:
	void onExpanded(const QModelIndex &index);
signals:
	void currentItemChanged(FolderTreeItem *current, FolderTreeItem *previous);
	void expanded(FolderTreeItem *item);
	
	// QWidget interface
protected:
	void paintEvent(QPaintEvent *event);
};

#endif // FOLDERTREEVIEW_H
