#include "FileTableView.h"
#include "ApplicationGlobal.h"
#include "MainWindow.h"
#include "common/str.h"
#include <QItemDelegate>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QMouseEvent>
#include <QApplication>
#include <QHeaderView>
#include "IncrementalSearchHelper.h"

namespace {

QRect drawItemViewText(bool strong_suffix, QStyle *s, QPainter *p, const QStyleOptionViewItem *option, bool abbreviation, incrementalsearch::Result const &match)
{
	bool enabled = (option->state & QStyle::State_Enabled);
	p->save();
	p->setFont(option->font);
	
	int flags = option->displayAlignment;
	QString name = option->text;
	QString suffix;
	int suffix_offset = name.size();
	QRect rSuffix;
	
	if (strong_suffix) {
		int i = name.lastIndexOf('.');
		if (i > 0) {
			suffix = name.mid(i + 1);
			name = name.left(i + 1);
			suffix_offset = i + 1;
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
	
	int visible_length = name.size();
	QString ellipsis;
	QFontMetrics fm = p->fontMetrics();
	if (abbreviation && fm.horizontalAdvance(name) + rSuffix.width() > option->rect.width()) {
		ellipsis = "...";
		while (visible_length > 0 && fm.horizontalAdvance(name.left(visible_length) + ellipsis) + rSuffix.width() > option->rect.width()) {
			--visible_length;
			if (visible_length > 0 && name.at(visible_length - 1).isHighSurrogate()) --visible_length;
		}
		name = name.left(visible_length) + ellipsis;
	}
	auto name_parts = incrementalsearch::textFragments(match, 0, visible_length);
	if (!ellipsis.isEmpty()) name_parts.push_back({ellipsis, false});
	
	QRect rName = p->fontMetrics().boundingRect(option->rect, flags, name);
	incrementalsearch::drawText_fragments(p, *option, rName, name_parts);

	QRect ret = rName;
	
	if (strong_suffix && !suffix.isEmpty()) {
		p->save();
		QFont font = option->font;
		font.setBold(true);
		p->setFont(font);
		QRect r = rSuffix.translated(rName.width(), 0);
		incrementalsearch::drawText_fragments(p, *option, r, incrementalsearch::textFragments(match, suffix_offset));
		p->restore();

		ret = ret.united(r);
	}
	
	p->restore();

	return ret;
}

} // namespace

class FileTableItemDelegate : public QStyledItemDelegate {
public:
	Kind kind_ = Kind::File;
	QString path_;

	FileTableItemDelegate(QObject *parent = nullptr)
		: QStyledItemDelegate(parent)
	{
	}
	
	void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
	{
		if (FileTableView const *w = qobject_cast<FileTableView const *>(option.widget)) {
			FileInfo2 const *fileinfo = w->model()->fileinfo(index);
			if (!fileinfo) return;
			// Q_ASSERT(fileinfo);

			QRect rIcon;
			
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
				rIcon = o.rect;
				rIcon.setWidth(rIcon.height());

				QString suffix = fileinfo->name;
				if (int i = suffix.lastIndexOf('.'); i > 0) {
					suffix = suffix.mid(i + 1);
				}
				if (!fileinfo->isdir && global->is_extension_archive_file(suffix)) {
					auto icon = global->makeArchiveFileIcon(suffix);
					icon.paint(painter, rIcon);
				} else {
					o.icon.paint(painter, rIcon);
				}
				o.rect.adjust(z, 0, 0, 0);
			}
			o.displayAlignment = Qt::AlignVCenter;
			if (col == 1 && kind_ != Kind::ChromeBookmark) {
				o.displayAlignment |= Qt::AlignRight;
			}
			bool strong_suffix = false;
			if (path_.startsWith((misc::str)prefix_bookmark)) {
				// pass
			} else if (fileinfo->isdir) {
				// pass
			} else {
				strong_suffix = global->appsettings.strongly_draw_file_suffix && (col == 0);
			}
			drawItemViewText(strong_suffix, w->style(), painter, &o, true, w->model()->matchText(index));
			{
				QPixmap tmp(1, 1);
				QPainter p2(&tmp);
				QRect r = drawItemViewText(strong_suffix, w->style(), &p2, &o, false, w->model()->matchText(index));
				if (rIcon.isValid()) {
					r = r.united(rIcon);
				}
				const_cast<FileTableView *>(w)->setColumnWidth(col, r.width() + 4);
			}
		}
	}
	void setPath(const QString &path)
	{
		path_ = path;
	}
};


struct FileTableView::Private {
	FileTableItemDelegate *item_delegate;
	Kind kind = Kind::File;
	std::vector<int> column_width;
	QTimer interval_timer_10ms;
	int adjust_column_width_debounce = 0;
	bool scrolled = false;
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

	// horizontalHeader()->setStretchLastSection(true);

	connect(&m->interval_timer_10ms, &QTimer::timeout, this, &FileTableView::onIntervalTimer10ms);
	m->interval_timer_10ms.start(10);
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

void FileTableView::adjustColumnWidth()
{
	int ncols = model()->columnCount();
	for (int col = 0; col < ncols; ++col) {
		if (col < m->column_width.size()) {
			int w = m->column_width[col];
			if (w > 0) {
				horizontalHeader()->resizeSection(col, w);
			}
		}
	}
}

void FileTableView::onIntervalTimer10ms()
{
	if (m->adjust_column_width_debounce > 0) {
		m->adjust_column_width_debounce--;
		if (m->adjust_column_width_debounce == 0) {
			adjustColumnWidth();
		}
	}
}

void FileTableView::scrollTo(const QModelIndex &index, ScrollHint hint)
{
	QTableView::scrollTo(index, hint);
	m->scrolled = true;
}

void FileTableView::resetColumnWidth()
{
	m->column_width.clear();
}

void FileTableView::setColumnWidth(int col, int width)
{
	int ncols = model()->columnCount();
	m->column_width.resize(ncols);
	if (col >= 0 && col < ncols) {
		if (m->column_width[col] < width) {
			m->column_width[col] = width;
		}
	}
	if (m->scrolled) {
		// ユーザーが horizontalHeader を操作してサイズ変更した場合はここには来ない
		m->scrolled = false;
		m->adjust_column_width_debounce = 50;
	}
}

void FileTableView::setPath(const QString &path)
{
	m->item_delegate->setPath(path);
}

void FileTableView::setKind(Kind kind)
{
	m->kind = kind;
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

	model()->setFilterText(text);

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
	resetColumnWidth();
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
	// resetColumnWidth();
	
	IncrementalSearchFilter const *f = &filter();
	if (f && *f) {
		QPainter pr(viewport());
		QColor filtered_bg_color = incrementalsearch::filtered_bg_color();
		pr.fillRect(rect(), filtered_bg_color);
	}
	
	QTableView::paintEvent(event);
}
