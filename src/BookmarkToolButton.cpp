#include "BookmarkToolButton.h"
#include <QDebug>
#include <QPainter>
#include <QPalette>
#include <QStyle>
#include <QPaintEvent>

BookmarkToolButton::BookmarkToolButton(QWidget *parent)
	: QToolButton(parent)
{
}

void BookmarkToolButton::paintEvent(QPaintEvent *event)
{
	QPainter pr(this);
	bool hover = rect().contains(mapFromGlobal(QCursor::pos()));
	if (hover) {
		pr.fillRect(rect(), QColor(0, 0, 0, 32));
	}
	int flags = Qt::AlignVCenter;
	int x = 4;
	style()->drawItemText(&pr, rect().adjusted(x, 0, 0, 0), flags, palette(), true, text(), QPalette::ButtonText);
}
