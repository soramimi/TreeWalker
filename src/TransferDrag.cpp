#include "TransferDrag.h"
#include "TransferClipboard.h"
#include <QApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QMouseEvent>
#include <QPainter>
#include <QWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QUuid>
#include <QHash>
#include <QLoggingCategory>

namespace filetransfer {
Q_LOGGING_CATEGORY(dragLog, "treewalker.transfer.drag", QtWarningMsg)
namespace {
const char *dragFormat = "application/x-treewalker-private-drag";
QHash<QByteArray, QStringList> activeDrags;

class DropIndicator : public QWidget {
public:
	explicit DropIndicator(QWidget *parent) : QWidget(parent)
	{
		setAttribute(Qt::WA_TransparentForMouseEvents);
		hide();
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter painter(this);
		painter.setPen(QPen(palette().color(QPalette::Highlight), 2));
		painter.setBrush(Qt::NoBrush);
		painter.drawRect(QRectF(rect()).adjusted(1, 1, -1, -1));
	}
};

}

DragSession::DragSession(QStringList const &paths, bool exposeFiles)
	: token_(QUuid::createUuid().toByteArray())
{
	activeDrags.insert(token_, paths);
	if (exposeFiles) {
		for (auto const &path : paths) {
			QFileInfo info(path);
			if (info.isAbsolute() && info.isFile()) urls_.push_back(QUrl::fromLocalFile(path));
		}
	}
}
DragSession::~DragSession() { activeDrags.remove(token_); }
std::unique_ptr<QMimeData> DragSession::mimeData() const
{
	auto mime = std::make_unique<QMimeData>();
	mime->setData(dragFormat, token_);
	if (!urls_.isEmpty()) mime->setUrls(urls_);
	return mime;
}

bool confirmDrop(Request *request, Qt::DropActions allowed, QWidget *parent)
{
	if (!request) return false;
	QString error = validate(request);
	if (!error.isEmpty()) { QMessageBox::warning(parent, QObject::tr("Cannot drop here"), error); return false; }
	QMessageBox box(QMessageBox::Question, QObject::tr("Move or copy?"),
		QObject::tr("%1 item(s)\nDestination: %2\nChoose an operation.").arg(request->sources.size()).arg(request->destination), QMessageBox::NoButton, parent);
	auto *move = box.addButton(QObject::tr("Move"), QMessageBox::ActionRole);
	auto *copy = box.addButton(QObject::tr("Copy"), QMessageBox::ActionRole);
	auto *cancel = box.addButton(QObject::tr("Cancel"), QMessageBox::RejectRole);
	box.setDefaultButton(cancel); box.setEscapeButton(cancel);
	box.exec();
	if (box.clickedButton() == move) request->operation = Operation::Move;
	else if (box.clickedButton() == copy) request->operation = Operation::Copy;
	else return false;
	Qt::DropAction action = request->operation == Operation::Move ? Qt::MoveAction : Qt::CopyAction;
	if (!(allowed & action) || !(allowed & Qt::CopyAction)) {
		QMessageBox::warning(parent, QObject::tr("Operation unavailable"), QObject::tr("The drag source does not allow this operation or a safe transfer acknowledgment. No files were changed."));
		return false;
	}
	return true;
}

DragController::DragController(QAbstractItemView *view, bool tree, std::function<QString()> directory, Handler handler)
	: QObject(view), view_(view), tree_(tree), directory_(std::move(directory)), handler_(std::move(handler)), indicator_(new DropIndicator(view->viewport()))
{
	view->setAcceptDrops(true); view->viewport()->setAcceptDrops(true);
	view->setDragEnabled(false); // The controller owns the complete file protocol.
	view->viewport()->installEventFilter(this);
	view->installEventFilter(this);
	scroll_.setInterval(60);
	connect(&scroll_, &QTimer::timeout, this, [this] {
		auto *bar = view_->verticalScrollBar();
		if (hover_.y() < 24) bar->setValue(bar->value() - bar->singleStep());
		else if (hover_.y() > view_->viewport()->height() - 24) bar->setValue(bar->value() + bar->singleStep());
		indicate(hover_);
	});
}
Request DragController::requestFor(QMimeData const *mime, QPoint const &position) const
{
	Request request;
	if (!mime) return request;
	if (mime->hasFormat(dragFormat)) request.sources = activeDrags.value(mime->data(dragFormat));
	else if (external_) request = clipboardRequest(mime);
	request.operation = Operation::Copy; // No modifier or proposed action decides the operation.
	request.destination = destinationPath(view_->indexAt(position), directory_(), tree_, true);
	return request;
}
void DragController::indicate(QPoint const &position)
{
	auto index = view_->indexAt(position);
	if (destinationPath(index, directory_(), tree_, true).isEmpty()) { indicator_->hide(); return; }
	QRect rect = index.isValid() ? view_->visualRect(index) : view_->viewport()->rect().adjusted(2, 2, -2, -2);
	indicator_->setGeometry(rect); indicator_->show();
}
bool DragController::eventFilter(QObject *watched, QEvent *event)
{
	if (watched != view_->viewport()) return QObject::eventFilter(watched, event);
	if (event->type() == QEvent::MouseButtonPress) {
		auto *mouse = static_cast<QMouseEvent *>(event);
		pressed_ = mouse->button() == Qt::LeftButton && view_->indexAt(mouse->position().toPoint()).isValid();
		press_ = mouse->position().toPoint();
	} else if (event->type() == QEvent::MouseButtonRelease) pressed_ = false;
	else if (event->type() == QEvent::MouseMove) {
		auto *mouse = static_cast<QMouseEvent *>(event);
		if (pressed_ && (mouse->buttons() & Qt::LeftButton) && (mouse->position().toPoint() - press_).manhattanLength() >= QApplication::startDragDistance()) {
			pressed_ = false;
			auto paths = selectedPaths(view_);
			if (paths.isEmpty()) return false;
			DragSession session(paths, !tree_);
			QPointer<QDrag> drag = new QDrag(view_);
			drag->setMimeData(session.mimeData().release());
			drag->exec(Qt::CopyAction);
			if (drag) drag->deleteLater();
			return true;
		}
	} else if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove) {
		auto *drag = static_cast<QDragMoveEvent *>(event);
		auto request = requestFor(drag->mimeData(), drag->position().toPoint());
		const auto actions = drag->possibleActions();
		const bool entering = event->type() == QEvent::DragEnter;
		qCDebug(dragLog) << "drag" << event->type() << actions << drag->mimeData()->formats() << request.sources << request.destination;
		// Qt needs an accepted enter event to route later moves within the view.
		if (!(actions & (Qt::CopyAction | Qt::MoveAction)) || request.sources.isEmpty()
				|| (!entering && !validate(&request).isEmpty())) {
			drag->ignore();
			indicator_->hide();
			scroll_.stop();
			return true;
		}
		if (!entering) {
			hover_ = drag->position().toPoint();
			indicate(hover_);
			scroll_.start();
		}
		// Copy is the transport acknowledgment; only our backend may delete sources.
		drag->setDropAction((actions & Qt::CopyAction) ? Qt::CopyAction : Qt::MoveAction);
		drag->accept();
		return true;
	} else if (event->type() == QEvent::DragLeave) { indicator_->hide(); scroll_.stop(); event->accept(); return true; }
	else if (event->type() == QEvent::Drop) {
		auto *drop = static_cast<QDropEvent *>(event);
		indicator_->hide(); scroll_.stop();
		auto request = requestFor(drop->mimeData(), drop->position().toPoint());
		auto allowed = drop->possibleActions();
		// An active TreeWalker session can move files through our own transfer job.
		if ((allowed & Qt::CopyAction) && drop->mimeData() && activeDrags.contains(drop->mimeData()->data(dragFormat))) {
			allowed |= Qt::MoveAction;
		}
		qCDebug(dragLog) << "drop" << drop->possibleActions() << request.sources << request.destination;
		if (validate(&request).isEmpty() && handler_(request, allowed) && (drop->possibleActions() & Qt::CopyAction)) {
			drop->setDropAction(Qt::CopyAction); drop->accept();
		} else drop->ignore();
		return true;
	}
	return QObject::eventFilter(watched, event);
}
}
