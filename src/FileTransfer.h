#ifndef FILETRANSFER_H
#define FILETRANSFER_H

#include <QAbstractItemView>
#include <QStringList>

namespace filetransfer {

enum class Operation { Copy, Move };
struct Request {
	QStringList sources;
	QString destination;
	Operation operation = Operation::Copy;
};

// Empty error means a valid, normalized request. Never follows a source link.
QString validate(Request *request);
QStringList selectedPaths(QAbstractItemView const *view);
QString destinationPath(QModelIndex const &index, QString const &background, bool tree, bool drop);
QString pathKey(QString const &path);

}
#endif
