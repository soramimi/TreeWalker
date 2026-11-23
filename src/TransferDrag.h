#ifndef TRANSFERDRAG_H
#define TRANSFERDRAG_H
#include "FileTransfer.h"
#include <QPointer>
#include <QMimeData>
#include <QTimer>
#include <QUrl>
#include <functional>
namespace filetransfer {
class DragSession {
	QByteArray token_;
	QList<QUrl> urls_;
public:
	DragSession(QStringList const &paths, bool exposeFiles);
	~DragSession();
	DragSession(DragSession const &) = delete;
	DragSession &operator=(DragSession const &) = delete;
	std::unique_ptr<QMimeData> mimeData() const;
};
bool confirmDrop(Request *request, Qt::DropActions allowed, QWidget *parent);
class DragController : public QObject {
public:
	using Handler = std::function<bool(Request, Qt::DropActions)>;
	DragController(QAbstractItemView *view, bool tree, std::function<QString()> directory, Handler handler);
	void allowExternalDrops(bool allowed) { external_ = allowed; }
protected:
	bool eventFilter(QObject *watched, QEvent *event) override;
private:
	QAbstractItemView *view_;
	bool tree_, pressed_ = false, external_ = false;
	QPoint press_, hover_;
	std::function<QString()> directory_;
	Handler handler_;
	QWidget *indicator_;
	QTimer scroll_;
	Request requestFor(QMimeData const *mime, QPoint const &position) const;
	void indicate(QPoint const &position);
};
}
#endif
