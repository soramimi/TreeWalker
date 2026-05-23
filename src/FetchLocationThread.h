#ifndef FETCHLOCATIONTHREAD_H
#define FETCHLOCATIONTHREAD_H

#include "MainWindow.h"

#include <QThread>

class FetchLocationThread : public QThread {
	Q_OBJECT
public:
	LocationData data;
	void run();
signals:
	void done(LocationData const &data);
};


#endif // FETCHLOCATIONTHREAD_H
