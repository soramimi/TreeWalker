#ifndef MYLISTWIDGET_H
#define MYLISTWIDGET_H

#include <QListWidget>
#include <QWidget>

class MyListWidget : public QListWidget
{
	Q_OBJECT
private:
	struct Private;
	Private *m;
public:
	explicit MyListWidget(QWidget *parent = nullptr);

	~MyListWidget();
signals:

public slots:
};

#endif // MYLISTWIDGET_H
