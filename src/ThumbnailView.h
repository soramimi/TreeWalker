#ifndef THUMBNAILVIEW_H
#define THUMBNAILVIEW_H

#include <QListView>
#include <QWidget>

#include "FileItemModel.h"

class MainWindow;
class ThumbnailViewDelegate;

class ThumbnailView : public QListView {
	Q_OBJECT
	friend class MainWindow;
	friend class ThumbnailViewDelegate;
private:
	struct Private;
	Private *m;

	QImage queryThubmanil(const QString &text);
	void _set_filter(const QString &filter_text);
protected:
	void beginResetModel();
	void endResetModel();
	IncrementalSearchFilter const &filter() const;
public:
	FileItemModel *model();
	FileItemModel const *model() const;
	ThumbnailView(QWidget *parent);
	~ThumbnailView();
	MainWindow *mainwindow();
	MainWindow const *mainwindow() const;
	void setKind(Kind kind);
	void updateThumbnail(QString const &path, QImage const &image);
	QString currentPath() const;
	void selectRow(int row);
	void setLocation(QString const &path);

	void selectFirstItem();
	
	void setFilterText(QString const &text);

	// void setFilter(const QString &filter_text);
	
	// QWidget interface
protected:
	void paintEvent(QPaintEvent *event);
};

#endif // THUMBNAILVIEW_H
