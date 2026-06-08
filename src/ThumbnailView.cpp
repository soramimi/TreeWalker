#include "MainWindow.h"
#include "ThumbnailView.h"
#include "ApplicationGlobal.h"
#include <QApplication>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QTextDocument>
#include "darktheme/MyCommonStyle.h"

#if 0
class ThumbnailListModel : public QAbstractListModel {
public:
	ThumbnailListModel(QObject *parent);
	int rowCount(const QModelIndex &parent) const;
	QVariant data(const QModelIndex &index, int role) const;
};
#endif

//

class ThumbnailViewDelegate : public QStyledItemDelegate {
public:
	Kind kind_ = Kind::File;
	QString location;
	ThumbnailViewDelegate(QWidget *parent = nullptr)
		: QStyledItemDelegate(parent)
	{
	}
public:
	void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
	{
		ThumbnailView *widget = qobject_cast<ThumbnailView *>(parent());
		QStyle *s = qApp->style();

		QStyleOptionViewItem o1;
		initStyleOption(&o1, index);
		o1.state = QStyle::State_Selected | QStyle::State_Active;
		o1.rect = option.rect;//.adjusted(4, 4, -4, -4);
		o1.showDecorationSelected = true;
		int f = 0;
		double alpha = 0;
		bool selected = option.state & QStyle::State_Selected;
		bool mouseover = option.state & QStyle::State_MouseOver;
		if (selected)  f |= 1;
		if (mouseover) f |= 2;
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
			if (selected) {
				MyCommonStyleBase::drawFrame(painter, o1.rect, Qt::black, Qt::black);
			}
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
		QIcon icon;
		{
			QString text = index.data(PathRole).toString();
			if (!text.isEmpty()) {
				QImage image = widget->queryThubmanil(text);
				if (!image.isNull()) {
					icon = QIcon(QPixmap::fromImage(image));
				}
			}
		}
		if (icon.isNull()) {
			icon = o2.icon;
		}
		icon.paint(painter, x, y, w, h);

		QTextOption textopt;
		textopt.setAlignment((Qt::Alignment)(Qt::AlignCenter | Qt::AlignBottom));
		QString name = index.data(Qt::DisplayRole).toString();
		QString suffix;

		// wip: bold rendering of file extensions
		
		bool strong_suffix = true;
		if (strong_suffix) {
			int i = name.lastIndexOf('.');
			if (i > 0) {
				suffix = name.mid(i + 1);
				name = name.left(i + 1);
			}
		}

		{
			QTextDocument doc;
			QString html = "<center>";
			html += name.toHtmlEscaped();
			if (strong_suffix && !suffix.isEmpty()) {
				html += "<b>" + suffix.toHtmlEscaped() + "</b>";
			}
			html += "</center>";
			doc.setHtml(html);
			doc.setDefaultFont(o2.font);
			doc.setTextWidth(o2.rect.width());
			int h = doc.size().height();
			painter->save();
			QRect r(o2.rect.x(), o2.rect.y() + o2.rect.height() - h, o2.rect.width(), h);
			painter->fillRect(r, QColor(255, 255, 255, 128));
			painter->translate(r.x(), r.y());
			doc.drawContents(painter);
			painter->restore();
		}
	}
	void setLocation(const QString &loc);
};


struct ThumbnailView::Private {
	// FileItemModel model;
	ThumbnailViewDelegate item_delegate;
};

FileItemModel *ThumbnailView::model()
{
	// return &m->model;
	return global->mainwindow->fileitemmodel();
}

const FileItemModel *ThumbnailView::model() const
{
	return const_cast<ThumbnailView *>(this)->model();
}

ThumbnailView::ThumbnailView(QWidget *parent)
	: QListView(parent)
	, m(new Private)
{
	setItemDelegate(&m->item_delegate);

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
}

MainWindow *ThumbnailView::mainwindow()
{
	return global->mainwindow;
}

MainWindow const *ThumbnailView::mainwindow() const
{
	return global->mainwindow;
}

ThumbnailView::~ThumbnailView()
{
	delete m;
}

void ThumbnailView::setKind(Kind kind)
{
	m->item_delegate.kind_ = kind;
}

void ThumbnailView::updateThumbnail(const QString &path, const QImage &image)
{
	viewport()->update();
}

QString ThumbnailView::currentPath() const
{
	auto indexes = selectionModel()->selectedIndexes();
	if (indexes.size() > 0) {
		return model()->data(indexes[0], PathRole).toString();
	}
	return QString();
}

void ThumbnailView::selectRow(int row)
{
	auto index = model()->index(row, 0);
	QItemSelectionModel *selectionmodel = selectionModel();
	if (index.isValid() && selectionmodel) {
		setCurrentIndex(index);
		selectionmodel->select(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
		scrollTo(index);
	}
}

void ThumbnailView::setLocation(const QString &path)
{
	m->item_delegate.setLocation(path);	
}

QImage ThumbnailView::queryThubmanil(QString const &text)
{
	return mainwindow()->queryThumbnail(text);
}

#if 0
ThumbnailListModel::ThumbnailListModel(QObject *parent)
	: QAbstractListModel(parent)
{

}

int ThumbnailListModel::rowCount(const QModelIndex &parent) const
{
	return 1;
}

QVariant ThumbnailListModel::data(const QModelIndex &index, int role) const
{
	if (role == Qt::DisplayRole) {
		return QString("Thumbnail");
	} else if (role == Qt::SizeHintRole) {
		return QSize(128, 128);
	} else if (role == Qt::DecorationRole) {
		return QIcon(":/folder.png");
	} else if (role == PathRole) {
		return QString("/path/to/file");
	}
	return {};

}
#endif

void ThumbnailViewDelegate::setLocation(const QString &loc)
{
	location = loc;
}
