#ifndef FILETABLEVIEW_H
#define FILETABLEVIEW_H

#include "FileItemModel.h"

#include <QDebug>
#include <QTableView>



class FileTableView : public QTableView {
	Q_OBJECT
	friend class MainWindow;
	friend class FileTableItemDelegate;
private:
	struct Private;
	Private *m;
	void _set_filter(const QString &filter_text);
	IncrementalSearchFilter const &filter() const;
	void resetColumnWidth();
	void setColumnWidth(int col, int width);
	void adjustColumnWidth();
protected:
	void beginResetModel();
	void endResetModel();
public:
	FileTableView(QWidget *parent);
	~FileTableView();

	FileItemModel *model();
	FileItemModel const *model() const;

	void setPath(QString const &path);
	
	void setKind(Kind kind);
	Kind kind() const;

	QString currentPath() const;
	// QWidget interface
	
	void selectFirstItem();

	void setFilterText(const QString &text);
	
	void setCurrentTop();
	void setCurrentBottom();
	// void setFilter(const QString &filter_text);
	
protected:
	void mouseDoubleClickEvent(QMouseEvent *e);
signals:
	void itemDoubleClicked(QModelIndex const &index);

	// QWidget interface
protected:
	void paintEvent(QPaintEvent *event);
private slots:
	void onIntervalTimer10ms();
	
	// QAbstractItemView interface
public:
	void scrollTo(const QModelIndex &index, ScrollHint hint = EnsureVisible) override;
};

#endif // FILETABLEVIEW_H
