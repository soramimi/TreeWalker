#ifndef STATUSLABEL_H
#define STATUSLABEL_H

#include <QLabel>

class StatusLabel : public QLabel {
	Q_OBJECT
public:
	explicit StatusLabel(QWidget *parent = nullptr);
	QSize sizeHint() const
	{
		return QSize(0, QLabel::sizeHint().height());
	}
};

#endif // STATUSLABEL_H
