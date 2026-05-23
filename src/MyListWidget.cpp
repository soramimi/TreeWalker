#include "MyListWidget.h"

#include <QItemDelegate>
#include <QStyledItemDelegate>
#include <QApplication>
#include <QPainter>
#include <QDebug>

class MyListItemDelegate : public QStyledItemDelegate {
private:
public:
	MyListItemDelegate(MyListWidget *parent)
		: QStyledItemDelegate(parent)
	{

	}

	// QAbstractItemDelegate interface
public:
	void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
	{
		QStyleOptionViewItem o1;
		initStyleOption(&o1, index);
		o1.state = QStyle::State_Selected | QStyle::State_Active;
		o1.rect = option.rect;//.adjusted(4, 4, -4, -4);
		o1.showDecorationSelected = true;
		int f = 0;
		double alpha = 0;
		if (option.state & QStyle::State_Selected)  f |= 1;
		if (option.state & QStyle::State_MouseOver) f |= 2;
		switch (f) {
		case 1: alpha = 0.8; break;
		case 2: alpha = 0.5;  break;
		case 3: alpha = 1.0;  break;
		}
		if (alpha > 0) {
			painter->save();
			painter->setOpacity(alpha);
			qApp->style()->drawPrimitive(QStyle::PE_PanelItemViewItem, &o1, painter, 0);
			painter->restore();
		}

		QStyleOptionViewItem o2 = option;
		initStyleOption(&o2, index);
		o2.state &= ~QStyle::State_HasFocus;
		o2.state &= ~QStyle::State_MouseOver;
		o2.state &= ~QStyle::State_Selected;
		o2.text = QString();

		int x = o2.rect.x() + 4;
		int y = o2.rect.y() + 4;
		int w = o2.rect.width() - 8;
		int h = w * 3 / 4;
		o2.icon.paint(painter, x, y, w, h);

		QTextOption textopt;
		textopt.setAlignment((Qt::Alignment)Qt::AlignCenter);
		QString text = index.data(Qt::DisplayRole).toString();
		QRect r(x, y + h, w, o2.rect.height() - h - 4);
		painter->drawText(r, text, textopt);
	}
};

struct MyListWidget::Private {
	MyListItemDelegate *item_delegate;
};


MyListWidget::MyListWidget(QWidget *parent)
	: QListWidget(parent)
	, m(new Private)
{
	m->item_delegate = new MyListItemDelegate(this);
	setItemDelegate(m->item_delegate);

	setEditTriggers(QListView::NoEditTriggers);
	setSelectionMode(QListView::ExtendedSelection);
	setSelectionBehavior(QListView::SelectRows);
	setMovement(QListView::Static);
	setFlow(QListView::LeftToRight);
	setWrapping(true);
	setResizeMode(QListView::Adjust);
	setViewMode(QListView::IconMode);
	setSpacing(2);
	setSelectionRectVisible(true);
	setHorizontalScrollMode(QListView::ScrollPerPixel);
	setVerticalScrollMode(QListView::ScrollPerPixel);
	setDragDropMode(QListView::DragOnly);
	setDragEnabled(true);
	setIconSize(QSize(128, 128));
}

MyListWidget::~MyListWidget()
{
	delete m;
}

