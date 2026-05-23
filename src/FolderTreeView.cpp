#include "FolderTreeView.h"
#include <QKeyEvent>
#include <QDebug>
#include "FileItemModel.h"

void FolderTreeItem::clearChildrenCache()
{
	children_cache_.clear();
}

void FolderTreeItem::updateChildrenCache()
{
	if (children_.empty()) {
		clearChildrenCache();
	} else if (children_cache_.empty()) { // only update if cache is empty
		for (size_t i = 0; i < children_.size(); i++) {
			children_cache_[children_[i]] = i;
		}
	}
}

FolderTreeItem *FolderTreeItem::parent()
{
	return parent_;
}

FolderTreeItem *FolderTreeItem::child(int i) const
{
	if (i >= 0 && i < children_.size()) {
		return children_[i];
	}
	return {};
}

class FolderTreeModel : public QAbstractItemModel {
	friend class FolderTreeView;
	friend class FolderTreeItem;
private:
	struct item_set_less {
		using is_transparent = void; // for heterogeneous lookup
		bool operator()(FolderTreeItem *const &a, FolderTreeItem *const &b) const
		{
			return (void *)a < (void *)b;
		}
		bool operator()(FolderTreeItem *const &a, void *b) const
		{
			return (void *)a < b;
		}
		bool operator()(void *b, FolderTreeItem *const &a) const
		{
			return b < (void *)a;
		}
	};
	std::set<FolderTreeItem *, item_set_less> item_set_;
	FolderTreeItem top_level_items_;
private:
	void _insertChild(FolderTreeItem *child)
	{
		item_set_.insert(child);
		child->setModel(this);
	}
	void _addChild(FolderTreeItem *parent, FolderTreeItem *child)
	{
		Q_ASSERT(child);
		if (!parent) {
			parent = &top_level_items_;
		}
		parent->addChild(child);
		Q_ASSERT(!parent->model() || parent->model() == this);
		parent->setModel(this);
		auto SetModel = [&](auto self, FolderTreeItem *item)-> void {
			Q_ASSERT(!item->model() || item->model() == this); // should not belong to another model
			_insertChild(item);
			for (FolderTreeItem *c : *item->children()) {
				self(self, c); // recursive call for children
			}
		};
		SetModel(SetModel, child);
	}
	void _detach(FolderTreeItem *item)
	{
		item_set_.erase(item);
		item->setModel(nullptr);
		for (FolderTreeItem *child : *item->children()) {
			_detach(child);
		}
	}
	void addTopLevelItem(FolderTreeItem *item)
	{
		_addChild({}, item);
	}
	FolderTreeItem *find(void *p) const
	{
		auto it = item_set_.find(p);
		if (it != item_set_.end()) {
			return *it;
		}
		return {};
	}
	FolderTreeItem *find(QModelIndex const &index)
	{
		if (!index.isValid()) {
			int row = index.row();
			if (row >= 0 && row < top_level_items_.children()->size()) {
				return top_level_items_.children()->at(row);
			}
		} else {
			return find(index.internalPointer());
		}
		return {};
	}
	FolderTreeItem *find(QModelIndex const &index) const
	{
		return const_cast<FolderTreeModel *>(this)->find(index);
	}
public:
	FolderTreeModel()
	{
		top_level_items_.setModel(this);
	}
	~FolderTreeModel()
	{
		clear();
	}
	void clear()
	{
		beginResetModel();

		top_level_items_.children()->clear();

		for (FolderTreeItem *child : item_set_) {
			delete child;
		}
		item_set_.clear();

		endResetModel();
	}
	QModelIndex index(int row, int column, const QModelIndex &parent) const
	{
		if (!parent.isValid()) {
			if (row >= 0 && row < top_level_items_.children()->size()) {
				return createIndex(row, column, top_level_items_.children()->at(row));
			}
		} else {
			if (FolderTreeItem *p = find(parent.internalPointer())) {
				if (row >= 0 && row < p->children()->size()) {
					FolderTreeItem *child = p->children()->at(row);
					return createIndex(row, column, child);
				}
			}
		}
		return {};
	}
	QModelIndex parent(const QModelIndex &child) const
	{
		if (FolderTreeItem *item = find(child.internalPointer())) {
			if (FolderTreeItem *parent = item->parent()) {
				return indexFromItem(parent);
			}
		}
		return {};
	}
	int rowCount(const QModelIndex &parent) const
	{
		if (!parent.isValid()) {
			return top_level_items_.children()->size();
		}
		if (auto item = find(parent)) {
			int n = item->children()->size();
			return n;
		}
		return 0;
	}
	int columnCount(const QModelIndex &parent) const
	{
		return 1;
	}
	QVariant data(const QModelIndex &index, int role) const
	{
		if (auto item = find(index)) {
			return item->data(0, role);
		}
		return {};
	}
	QMap<int, QVariant> itemData(const QModelIndex &index) const
	{
		if (auto item = find(index)) {
			return item->data_map();
		}
		return {};
	}
	FolderTreeItem *itemFromIndex(const QModelIndex &index) const
	{
		if (!index.isValid()) {
			int row = index.row();
			if (row >= 0 && row < top_level_items_.children()->size()) {
				return top_level_items_.children()->at(row);
			}
		}
		return find(index.internalPointer());
	}
	QModelIndex indexFromItem(FolderTreeItem *item) const
	{
		if (item) {
			auto it = item_set_.find(item);
			if (it != item_set_.end() && *it == item) {
				int row = 0;
				FolderTreeItem const *parent = item->parent();
				if (!parent) {
					parent = &top_level_items_;
					if (!parent) return {};
				}
				// find the row number of this item from the parent
				const_cast<FolderTreeItem *>(parent)->updateChildrenCache();
				auto it2 = parent->children_cache_.find(item);
				if (it2 != parent->children_cache_.end()) {
					row = it2->second;
				}
				return createIndex(row, 0, item);
			}
		}
		return {};
	}
	FolderTreeItem *itemFromIidl(ItemIdList iidl) const
	{
		for (FolderTreeItem *item : item_set_) {
			if (item->iidl() == iidl) {
				return item;
			}
		}
		return {};
	}
};

void FolderTreeItem::addChild(FolderTreeItem *child)
{
	child->parent_ = this;
	children_.push_back(child);
	clearChildrenCache(); // invalidate cache
	if (model_) {
		model_->_insertChild(child);
	}
}

FolderTreeItem *FolderTreeItem::takeChild(int row)
{
	if (row >= 0 && row < children_.size()) {
		FolderTreeItem *child = children_[row];
		children_.erase(children_.begin() + row);
		clearChildrenCache();
		if (model_) {
			model_->_detach(child);
		}
		return child;
	}
}

QString FolderTreeItem::text() const
{
	return text_;
}

void FolderTreeItem::setText(int column, const QString &text)
{
	setData(column, Qt::DisplayRole, text);
}

void FolderTreeItem::setData(int column, int role, const QVariant &data)
{
	if (role == Qt::DisplayRole) {
		text_ = data.toString();
	} else if (role == IidlRole) {
		iidl_ = data.value<ItemIdList>();
	}
	data_map_[role] = data;
}

QVariant FolderTreeItem::data(int column, int role) const
{
	if (role == Qt::DisplayRole) {
		return text();
	}
	if (role == IidlRole) {
		return QVariant::fromValue(iidl());
	}
	auto it = data_map_.find(role);
	if (it != data_map_.end()) {
		return it.value();
	}
	return {};
}

struct FolderTreeView::Private {
	FolderTreeModel model;
};

FolderTreeView::FolderTreeView(QWidget *parent)
	: QTreeView(parent)
	, m(new Private)
{
	setModel(&m->model);
	setHeaderHidden(true);
	setRootIsDecorated(false);
	setSelectionBehavior(QAbstractItemView::SelectRows);
	expandToDepth(0);

	connect(this, &QTreeView::expanded, this, &FolderTreeView::onExpanded);
}

FolderTreeView::~FolderTreeView()
{
	delete m;
}

void FolderTreeView::clear()
{
	beginResetModel();
	m->model.clear();
	endResetModel();
}

QModelIndex FolderTreeView::indexFromItem(FolderTreeItem *item) const
{
	return m->model.indexFromItem(item);
}

FolderTreeItem *FolderTreeView::itemFromIndex(const QModelIndex &index) const
{
	return m->model.itemFromIndex(index);
}

FolderTreeItem *FolderTreeView::itemFromIidl(ItemIdList iidl) const
{
	return m->model.itemFromIidl(iidl);
}

void FolderTreeView::addTopLevelItem(FolderTreeItem *item)
{
	m->model.addTopLevelItem(item);
}

FolderTreeItem *FolderTreeView::currentItem() const
{
	return itemFromIndex(currentIndex());
}

void FolderTreeView::setCurrentItem(FolderTreeItem *item)
{
	QModelIndex index;
	if (item) {
		index = indexFromItem(item);
		if (index == currentIndex()) return;
	}
	setCurrentIndex(index);
}

void FolderTreeView::beginResetModel()
{
	m->model.beginResetModel();
}

void FolderTreeView::endResetModel()
{
	m->model.endResetModel();
}

bool FolderTreeView::isExpanded(FolderTreeItem *item) const
{
	return isExpanded(indexFromItem(item));
}

void FolderTreeView::setExpanded(FolderTreeItem *item, bool f)
{
	setExpanded(currentIndex(), f);
}

void FolderTreeView::currentChanged(const QModelIndex &current, const QModelIndex &previous)
{
	scrollTo(current, EnsureVisible);
	FolderTreeItem *current_item = itemFromIndex(current);
	FolderTreeItem *previous_item = itemFromIndex(previous);
	setCurrentIndex(current);
	emit currentItemChanged(current_item, previous_item);
}

void FolderTreeView::onExpanded(const QModelIndex &index)
{
	FolderTreeItem *item = itemFromIndex(index);
	setCurrentItem(item);
	emit expanded(item);
}
