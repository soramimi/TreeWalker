#include "FileTableView.h"
#include "ApplicationGlobal.h""
#include "MainWindow.h"
#include <QItemDelegate>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QMouseEvent>
#include <QApplication>

void drawItemViewText(bool strong_suffix, QStyle *s, QPainter *p, const QStyleOptionViewItem *option, bool abbreviation)
{
	bool enabled = (option->state & QStyle::State_Enabled);
	p->save();
	p->setFont(option->font);
	QString name = option->text;
	QString suffix;
	if (strong_suffix) {
		int i = name.lastIndexOf('.');
		if (i > 0) {
			suffix = name.mid(i + 1);
			name = name.left(i + 1);
		}
	}
	int flags = option->displayAlignment;
	if (abbreviation) {
		int n = name.size();
		if (n > 1) {
			int w = option->rect.width();
			QFontMetrics fm = p->fontMetrics();
			if (fm.size(0, name).width() > w) {
				if (flags & Qt::AlignRight) {
					flags &= ~Qt::AlignRight;
					flags |= Qt::AlignLeft;
				}
				int lineheight = fm.height();
				while (n > 1) {
					if (fm.size(0, name + suffix).width() <= w) break;
					n--;
					name = name.mid(0, n);
					name += "...";
				}
			}
		}
	}
	QRect rName = p->fontMetrics().boundingRect(option->rect, flags, name);
	s->drawItemText(p, rName, flags, option->palette, enabled, name, QPalette::NoRole);
	if (strong_suffix && !suffix.isEmpty()) {
		p->save();
		QFont font = option->font;
		font.setBold(true);
		p->setFont(font);
		QRect rSuffix = p->fontMetrics().boundingRect(option->rect, flags, suffix).translated(rName.width(), 0);
		s->drawItemText(p, rSuffix, flags, option->palette, enabled, suffix, QPalette::NoRole);
		p->restore();
	}
	p->restore();
}

class FileTableItemDelegate : public QStyledItemDelegate {
public:
	Kind kind_ = Kind::File;
	QString location_;

	void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
	{
		if (FileTableView const *w = qobject_cast<FileTableView const *>(option.widget)) {
			FileInfo2 const *fileinfo = w->model()->fileinfo(index);
			Q_ASSERT(fileinfo);
			// Kind kind = (Kind)w->model()->data(index, KindRole).toInt();
#if 0
			int flags = Qt::AlignVCenter;
			if (index.column() == 1) {
				flags |= Qt::AlignRight;
			}
			w->style()->drawItemText(painter, option.rect.adjusted(2, 0, -3, 0), flags, option.palette, true, text);
#else
			int col = index.column();
			QStyleOptionViewItem o;// = option;
			initStyleOption(&o, index);
//			o.text = text;
			o.rect = option.rect.adjusted(2, 0, -2, 0);
			if (col == 0) {
				QRect r1(w->visualRect(w->model()->index(index.row(), 0)));
				QRect r2(w->visualRect(w->model()->index(index.row(), w->model()->columnCount() - 1)));
				painter->save();
				// painter->setOpacity(0.75);
				painter->setClipRect(o.rect);
				QStyleOptionViewItem o2 = o;
				o2.rect = r1.united(r2);
				// o2.state = QStyle::State_Selected | QStyle::State_Active;
				o2.state = option.state;
				o2.showDecorationSelected = true;
				// painter->drawEllipse(o2.rect);
				qApp->style()->drawPrimitive(QStyle::PE_PanelItemViewItem, &o2, painter, w);
				painter->restore();
			}
			if (col == 0) {
				int z = o.rect.height() * 5 / 4;
				QRect r = o.rect;
				r.setWidth(r.height());
				o.icon.paint(painter, r);
				o.rect.adjust(z, 0, 0, 0);
			}
			o.displayAlignment = Qt::AlignVCenter;
			if (col == 1 && kind_ != Kind::ChromeBookmark) {
				o.displayAlignment |= Qt::AlignRight;
			}
			bool strong_suffix = false;
			if (location_.startsWith(prefix_bookmark)) {
				// pass
			} else if (fileinfo->isdir) {
				// pass
			} else {
				strong_suffix = global->appsettings.strongly_draw_file_suffix && (col == 0);
			}
			drawItemViewText(strong_suffix, w->style(), painter, &o, true);
#endif
		}
	}
	void setLocation(const QString &loc)
	{
		location_ = loc;
	}
};


struct FileTableView::Private {
	// FileItemModel model;
	FileTableItemDelegate item_delegate;
	Kind kind = Kind::File;
};

FileTableView::FileTableView(QWidget *parent)
	: QTableView(parent)
	, m(new Private)
{
	setItemDelegate(&m->item_delegate);
	setShowGrid(false);
	setSelectionBehavior(QAbstractItemView::SelectRows);
	setSelectionMode(QAbstractItemView::ExtendedSelection);
}

FileTableView::~FileTableView()
{
	delete m;
}

FileItemModel *FileTableView::model()
{
	return global->mainwindow->fileitemmodel();
}

const FileItemModel *FileTableView::model() const
{
	return const_cast<FileTableView *>(this)->model();
}

void FileTableView::setLocation(const QString &loc)
{
	m->item_delegate.setLocation(loc);
}

void FileTableView::setKind(Kind kind)
{
	m->item_delegate.kind_ = kind;
}

Kind FileTableView::kind() const
{
	return m->kind;
}

QString FileTableView::currentPath() const
{
	auto indexes = selectionModel()->selectedRows();
	if (indexes.size() > 0) {
		return model()->data(indexes[0], PathRole).toString();
	}
	return QString();
}

void FileTableView::mouseDoubleClickEvent(QMouseEvent *e)
{
	auto index = indexAt(e->pos());
	emit itemDoubleClicked(index);
}

void FileTableView::paintEvent(QPaintEvent *event)
{
	QTableView::paintEvent(event);
}
