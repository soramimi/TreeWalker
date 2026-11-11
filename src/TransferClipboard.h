#ifndef TRANSFERCLIPBOARD_H
#define TRANSFERCLIPBOARD_H
#include "FileTransfer.h"
#include <QMimeData>
#include <memory>
namespace filetransfer {
std::unique_ptr<QMimeData> clipboardData(QStringList const &paths, Operation operation, QString const &token = {});
Request clipboardRequest(QMimeData const *mime);
QString clipboardToken(QMimeData const *mime);
}
#endif
