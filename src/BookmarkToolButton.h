#ifndef BOOKMARKTOOLBUTTON_H
#define BOOKMARKTOOLBUTTON_H

#include <QToolButton>



class BookmarkToolButton : public QToolButton {
private:
	QVariant property_;
public:
	BookmarkToolButton(QWidget *parent = 0);

	QVariant property() const
	{
		return property_;
	}
	void setProperty(const QVariant &prop)
	{
		property_ = prop;
	}
protected:
	void paintEvent(QPaintEvent *event);
private slots:
	void clicked();
};

#endif // BOOKMARKTOOLBUTTON_H
