#ifndef BOOKMARKTOOLBUTTON_H
#define BOOKMARKTOOLBUTTON_H

#include <QToolButton>



class BookmarkToolButton : public QToolButton {
public:
	BookmarkToolButton(QWidget *parent = 0);

	// QWidget interface
protected:
	void paintEvent(QPaintEvent *event);
};

#endif // BOOKMARKTOOLBUTTON_H
