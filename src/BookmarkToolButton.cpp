#include "BookmarkToolButton.h"
#include <QDebug>
#include <QPainter>
#include <QPalette>
#include <QStyle>
#include <QPaintEvent>
#include "ApplicationGlobal.h"
#include "MainWindow.h"

BookmarkToolButton::BookmarkToolButton(QWidget *parent)
	: QToolButton(parent)
{
	connect(this, &QToolButton::clicked, this, &BookmarkToolButton::clicked);
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

void BookmarkToolButton::clicked()
{
	global->mainwindow->openBookmark(property().value<BookmarkProperty>());
}
