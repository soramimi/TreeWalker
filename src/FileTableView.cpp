#include "FileTableView.h"
#include "ApplicationGlobal.h"
#include "MainWindow.h"
#include "common/str.h"
#include <QItemDelegate>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QMouseEvent>
#include <QApplication>
#include "IncrementalSearchHelper.h"

namespace {

void drawItemViewText(bool strong_suffix, QStyle *s, QPainter *p, const QStyleOptionViewItem *option, bool abbreviation, IncrementalSearchFilter const &filter)
{
	bool enabled = (option->state & QStyle::State_Enabled);
	p->save();
	p->setFont(option->font);
	
	int flags = option->displayAlignment;
	QString name = option->text;
	QString suffix;
	QRect rSuffix;
	
	if (strong_suffix) {
		int i = name.lastIndexOf('.');
		if (i > 0) {
			suffix = name.mid(i + 1);
			name = name.left(i + 1);
		}
	}

	if (!suffix.isEmpty()) {
		p->save();
		QFont font = option->font;
		if (strong_suffix) {
			font.setBold(true);
		}
		p->setFont(font);
		rSuffix = p->fontMetrics().boundingRect(option->rect, flags, suffix);
		p->restore();
	}
	
	if (abbreviation) {
		int n = name.size();
		constexpr int min_length = 1;
		if (n > min_length) {
			const int rect_width = option->rect.width();
			QFontMetrics fm = p->fontMetrics();
			auto Width = [&](){
				return fm.size(0, name).width() + rSuffix.width();
			};
			if (Width() > rect_width) {
				if (flags & Qt::AlignRight) {
					flags &= ~Qt::AlignRight;
					flags |= Qt::AlignLeft;
				}
				fm.height();
				while (n > min_length) {
					if (Width() <= rect_width) break;
					n--;
					name = name.mid(0, n);
					name += "...";
				}
			}
		}
	}

	auto DrawItemText = [&](QRect const &rect, QString const &text) {
		incrementalsearch::drawText_filtered(p, *option, rect, text, &filter);
	};
	
	QRect rName = p->fontMetrics().boundingRect(option->rect, flags, name);
	DrawItemText(rName, name);
	
	if (strong_suffix && !suffix.isEmpty()) {
		p->save();
		QFont font = option->font;
		font.setBold(true);
		p->setFont(font);
		QRect r = rSuffix.translated(rName.width(), 0);
		DrawItemText(r, suffix);
		p->restore();
	}
	
	p->restore();
}

} // namespace

class FileTableItemDelegate : public QStyledItemDelegate {
public:
	Kind kind_ = Kind::File;
	QString location_;

	FileTableItemDelegate(QObject *parent = nullptr)
		: QStyledItemDelegate(parent)
	{
	}
	
	void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
	{
		if (FileTableView const *w = qobject_cast<FileTableView const *>(option.widget)) {
			FileInfo2 const *fileinfo = w->model()->fileinfo(index);
			Q_ASSERT(fileinfo);

			int col = index.column();
			QStyleOptionViewItem o;
			initStyleOption(&o, index);
			o.rect = option.rect.adjusted(2, 0, -2, 0);
			if (col == 0) {
				QRect r1(w->visualRect(w->model()->index(index.row(), 0)));
				QRect r2(w->visualRect(w->model()->index(index.row(), w->model()->columnCount() - 1)));
				painter->save();
				painter->setClipRect(o.rect);
				QStyleOptionViewItem o2 = o;
				o2.rect = r1.united(r2);
				o2.state = option.state;
				o2.showDecorationSelected = true;
				qApp->style()->drawPrimitive(QStyle::PE_PanelItemViewItem, &o2, painter, w);
				painter->restore();
			}
			if (col == 0) {
				int z = o.rect.height() * 5 / 4;
				QRect r = o.rect;
				r.setWidth(r.height());

				QString suffix = fileinfo->name;
				if (int i = suffix.lastIndexOf('.'); i > 0) {
					suffix = suffix.mid(i + 1);
				}
				if (!fileinfo->isdir && global->is_extension_archive_file(suffix)) {
					auto icon = global->makeArchiveFileIcon(suffix);
					icon.paint(painter, r);
				} else {
					o.icon.paint(painter, r);
				}
				o.rect.adjust(z, 0, 0, 0);
			}
			o.displayAlignment = Qt::AlignVCenter;
			if (col == 1 && kind_ != Kind::ChromeBookmark) {
				o.displayAlignment |= Qt::AlignRight;
			}
			bool strong_suffix = false;
			if (location_.startsWith((misc::str)prefix_bookmark)) {
				// pass
			} else if (fileinfo->isdir) {
				// pass
			} else {
				strong_suffix = global->appsettings.strongly_draw_file_suffix && (col == 0);
			}
			drawItemViewText(strong_suffix, w->style(), painter, &o, true, w->filter());
		}
	}
	void setLocation(const QString &loc)
	{
		location_ = loc;
	}
};


struct FileTableView::Private {
	FileTableItemDelegate *item_delegate;
	Kind kind = Kind::File;
};

FileTableView::FileTableView(QWidget *parent)
	: QTableView(parent)
	, m(new Private)
{
	m->item_delegate = new FileTableItemDelegate(this);
	setItemDelegate(m->item_delegate);
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

FileItemModel const *FileTableView::model() const
{
	return const_cast<FileTableView *>(this)->model();
}

IncrementalSearchFilter const &FileTableView::filter() const
{
	return model()->filter();
}

void FileTableView::setLocation(const QString &loc)
{
	m->item_delegate->setLocation(loc);
}

void FileTableView::setKind(Kind kind)
{
	m->item_delegate->kind_ = kind;
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

void FileTableView::selectFirstItem()
{
	setCurrentIndex(model()->index(0, 0));
}

void FileTableView::setFilterText(QString const &text)
{
	int row = 0;
	if (text.isEmpty()) {
		row = model()->unfilteredRow(currentIndex().row());
	}

	beginResetModel();
	model()->setFilterText(text);
	endResetModel();

	if (row != -1) {
		auto index = model()->index(row, 0);
		setCurrentIndex(index);
		scrollTo(index);
	}
}

void FileTableView::setCurrentTop()
{
	auto index = model()->index(0, 0);
	setCurrentIndex(index);
	scrollTo(index);
}

void FileTableView::setCurrentBottom()
{
	auto index = model()->index(model()->rowCount() - 1, 0);
	setCurrentIndex(index);
	scrollTo(index);
}

void FileTableView::beginResetModel()
{
	model()->beginResetModel();
}
void FileTableView::endResetModel()
{
	model()->endResetModel();
}

void FileTableView::_set_filter(QString const &filter_text)
{
	model()->setFilterText(filter_text);
}

// void FileTableView::setFilter(const QString &filter_text)
// {
// 	beginResetModel();
// 	_set_filter(filter_text);
// 	endResetModel();
// }

void FileTableView::mouseDoubleClickEvent(QMouseEvent *e)
{
	auto index = indexAt(e->pos());
	emit itemDoubleClicked(index);
}

void FileTableView::paintEvent(QPaintEvent *event)
{
	IncrementalSearchFilter const *f = &filter();
	if (f && *f) {
		QPainter pr(viewport());
		QColor filtered_bg_color = incrementalsearch::filtered_bg_color();
		pr.fillRect(rect(), filtered_bg_color);
	}
	
	QTableView::paintEvent(event);
}
