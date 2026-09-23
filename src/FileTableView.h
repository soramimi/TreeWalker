#ifndef FILETABLEVIEW_H
#define FILETABLEVIEW_H

#include "FileItemModel.h"

#include <QDebug>
#include <QTableView>



class FileTableView : public QTableView {
	Q_OBJECT
	friend class FileTableItemDelegate;
private:
	struct Private;
	Private *m;
	void _set_filter(const QString &filter_text);
protected:
	void beginResetModel();
	void endResetModel();
public:
	FileTableView(QWidget *parent);
	~FileTableView();

	FileItemModel *model();
	FileItemModel const *model() const;

	void setLocation(QString const &loc);
	
	void setKind(Kind kind);
	Kind kind() const;

	QString currentPath() const;
	// QWidget interface
	
	
	
	void setFilter(const QString &filter_text);
	
protected:
	void mouseDoubleClickEvent(QMouseEvent *e);
signals:
	void itemDoubleClicked(QModelIndex const &index);

	// QWidget interface
protected:
	void paintEvent(QPaintEvent *event);
};

#endif // FILETABLEVIEW_H
