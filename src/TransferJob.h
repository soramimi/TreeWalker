#ifndef TRANSFERJOB_H
#define TRANSFERJOB_H
#include "FileTransfer.h"
#include "WorkerThread.h"
#include <QObject>
#include <QTimer>
#include <atomic>
#include <functional>
#include <memory>

namespace filetransfer {
enum class Choice { Skip, Rename, Replace, Cancel };
struct Decision { Choice choice = Choice::Cancel; QString name; };
enum class Outcome { Completed, Skipped, Failed, Canceled };
struct ItemResult { QString source, target; Outcome outcome = Outcome::Failed; QString error; };
using Results = QList<ItemResult>;
struct Callbacks {
	std::function<bool()> canceled;
	std::function<Decision(QString const &, QString const &, bool)> conflict;
	std::function<void(QString const &, qint64)> progress;
};
Results execute(Request request, Callbacks const &callbacks = {});

class Job : public QObject {
	Q_OBJECT
public:
	explicit Job(Request request, QObject *parent = nullptr);
	~Job();
	void start();
	void cancel();
	void resolve(Decision const &decision);
private:
	struct State;
	std::shared_ptr<State> state_;
	Request request_;
	WorkerThread worker_;
	QTimer timer_;
	bool started_ = false;
	void poll();
signals:
	void progress(QString const &path, qint64 bytes);
	void conflict(QString const &source, QString const &target, bool directory);
	void finished(filetransfer::Results const &results);
};
}
Q_DECLARE_METATYPE(filetransfer::Results)
#endif
