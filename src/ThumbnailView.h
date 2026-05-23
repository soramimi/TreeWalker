#ifndef THUMBNAILVIEW_H
#define THUMBNAILVIEW_H

#include <QListView>
#include <QWidget>

#include "FileItemModel.h"

class MainWindow;
class ThumbnailViewDelegate;

class ThumbnailView : public QListView {
	Q_OBJECT
	friend class ThumbnailViewDelegate;
private:
	struct Private;
	Private *m;

	QImage queryThubmanil(const QString &text);
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
};

#endif // THUMBNAILVIEW_H
