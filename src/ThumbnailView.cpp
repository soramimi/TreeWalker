#include "MainWindow.h"
#include "ThumbnailView.h"
#include "ApplicationGlobal.h"
#include <QApplication>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QTextDocument>
#include "darktheme/MyCommonStyle.h"
#include <IncrementalSearchHelper.h>

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

		QColor filtered_bg_color = incrementalsearch::filtered_bg_color();
		QColor highlight_bg_color = incrementalsearch::highlight_bg_color();
		
		IncrementalSearchFilter const *filter = &widget->filter();
		// if (filter && *filter) {
		// 	painter->fillRect(option.rect, filtered_bg_color);
		
		// }
		
		FileInfo2 const *fileinfo = widget->model()->fileinfo(index);
		Q_ASSERT(fileinfo);

		QString name = index.data(Qt::DisplayRole).toString();
		QString suffix;
		{
			int i = name.lastIndexOf('.');
			if (i > 0) {
				suffix = name.mid(i + 1);
				name = name.left(i + 1);
			}
		}

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
		if (!fileinfo->isdir && (suffix.compare("zip", Qt::CaseInsensitive) == 0 || suffix.compare("gz", Qt::CaseInsensitive) == 0)) {
			QString ext = suffix.toUpper();
			QPixmap pm = QPixmap::fromImage(global->zip_file_icon);
			{
				QPainter pr(&pm);
				pr.setFont(QFont("Arial", 40, QFont::Bold));
				QRect r = pr.fontMetrics().boundingRect(ext);
				r.adjust(-10, -2, 10, 2);
				r = QRect(pm.rect().width() - r.width() - 4, pm.rect().height() / 2, r.width(), r.height());
				pr.fillRect(r.translated(4, 4), Qt::black);
				pr.fillRect(r.adjusted(-1, -1, 1, 1), Qt::black);
				pr.fillRect(r.adjusted(1, 1, -1, -1), Qt::white);
				pr.setPen(Qt::black);
				// pr.drawText(r, Qt::AlignCenter, ext);
				incrementalsearch::drawText_filtered(&pr, o2, r, ext, filter);
			}
			icon = QIcon(pm);
		} else {
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

		// wip: bold rendering of file extensions
		
		bool strong_suffix = false;
		if (fileinfo->isdir) {
			// pass
		} else {
			strong_suffix = true;
		}

		QString text = name;
		if (filter && *filter) {
			incrementalsearch::Result match = global->incremental_search->match(text.toStdString(), *filter);
			if (match) {
				for (auto it = match.parts.rbegin(); it != match.parts.rend(); ++it) {
					if (it->match) {
						text.insert(it->pos + it->text.size(), "//>//");
						text.insert(it->pos, "//<//");
					}
				}
			}
		}

		{
			QTextDocument doc;
			QString html = text.toHtmlEscaped();
			if (filter && *filter) {
				QString bgcolor = QString::asprintf("#%02x%02x%02x"
													, highlight_bg_color.red()
													, highlight_bg_color.green()
													, highlight_bg_color.blue());
				html.replace("//&gt;//", "</span>");
				html.replace("//&lt;//", QString("<span style='background-color: %1;'>").arg(bgcolor));
			}
			html = "<center>" + html;
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
	ThumbnailViewDelegate item_delegate;
	// IncrementalSearchFilter filter;
};

FileItemModel *ThumbnailView::model()
{
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

void ThumbnailView::beginResetModel()
{
	model()->beginResetModel();
}

void ThumbnailView::endResetModel()
{
	model()->endResetModel();
}

const IncrementalSearchFilter &ThumbnailView::filter() const
{
	// return m->filter;
	return model()->filter();
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

void ThumbnailView::selectFirstItem()
{
	setCurrentIndex(model()->index(0, 0));
}

void ThumbnailView::paintEvent(QPaintEvent *event)
{
	IncrementalSearchFilter const *f = &filter();
	if (f && *f) {
		QPainter pr(viewport());
		QColor filtered_bg_color = incrementalsearch::filtered_bg_color();
		pr.fillRect(rect(), filtered_bg_color);
	}

	QListView::paintEvent(event);
}

void ThumbnailView::_set_filter(QString const &filter_text)
{
	model()->setFilterText(filter_text);
}

// void ThumbnailView::setFilter(const QString &filter_text)
// {
// 	beginResetModel();
// 	_set_filter(filter_text);
// 	endResetModel();
// }

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
