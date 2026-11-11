#include "TransferJob.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <sys/stat.h>
#endif

namespace filetransfer {
namespace {
namespace fs = std::filesystem;
fs::path native(QString const &path) { return fs::u8path(path.toUtf8().constData()); }
QString text(fs::path const &path) { return QString::fromUtf8(path.u8string().c_str()); }
struct Canceled {};
void check(Callbacks const &callbacks) { if (callbacks.canceled && callbacks.canceled()) throw Canceled{}; }
void fail(QString const &message) { throw std::runtime_error(message.toStdString()); }
bool entryExists(fs::path const &path) { return fs::exists(fs::symlink_status(path)); }
QString temporary(QString const &directory) { return QDir(directory).filePath(".treewalker-" + QUuid::createUuid().toString(QUuid::WithoutBraces)); }

// Never replaces a concurrently created destination.
bool renameExclusive(fs::path const &source, fs::path const &target, bool *cross_device = nullptr)
{
#ifdef Q_OS_WIN
	if (MoveFileExW(source.c_str(), target.c_str(), 0)) return true;
	if (cross_device) *cross_device = GetLastError() == ERROR_NOT_SAME_DEVICE;
#else
	if (syscall(SYS_renameat2, AT_FDCWD, source.c_str(), AT_FDCWD, target.c_str(), RENAME_NOREPLACE) == 0) return true;
	if (cross_device) *cross_device = errno == EXDEV;
#endif
	return false;
}

struct Snapshot {
	fs::path path;
	fs::file_type type;
	std::uintmax_t size;
	fs::file_time_type time;
	fs::path link;
	fs::perms permissions;
	quint64 device, identity;
};
Snapshot snapshot(fs::path const &path)
{
	auto status = fs::symlink_status(path);
	auto type = status.type();
	quint64 device = 0, identity = 0;
#ifdef Q_OS_WIN
	HANDLE handle = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
	if (handle == INVALID_HANDLE_VALUE) fail(QObject::tr("Cannot identify filesystem item."));
	BY_HANDLE_FILE_INFORMATION info{};
	bool identified = GetFileInformationByHandle(handle, &info);
	CloseHandle(handle);
	if (!identified) fail(QObject::tr("Cannot identify filesystem item."));
	device = info.dwVolumeSerialNumber; identity = (quint64(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
#else
	struct stat info{};
	if (::lstat(path.c_str(), &info) != 0) fail(QObject::tr("Cannot identify filesystem item."));
	device = info.st_dev; identity = info.st_ino;
#endif
	return {path, type, type == fs::file_type::regular ? fs::file_size(path) : 0,
		type == fs::file_type::symlink ? fs::file_time_type{} : fs::last_write_time(path),
		type == fs::file_type::symlink ? fs::read_symlink(path) : fs::path{}, status.permissions(), device, identity};
}
bool unchanged(Snapshot const &a)
{
	auto b = snapshot(a.path);
	return a.type == b.type && a.size == b.size && a.time == b.time && a.link == b.link && a.permissions == b.permissions && a.device == b.device && a.identity == b.identity;
}

void recordTree(fs::path const &path, std::vector<Snapshot> &entries, Callbacks const &callbacks)
{
	check(callbacks);
	auto entry = snapshot(path);
	entries.push_back(entry);
	if (entry.type == fs::file_type::directory) for (auto const &child : fs::directory_iterator(path)) recordTree(child.path(), entries, callbacks);
}
bool unchangedAt(std::vector<Snapshot> const &entries, fs::path const &original, fs::path const &current)
{
	for (auto entry : entries) {
		auto relative = entry.path.lexically_relative(original);
		entry.path = relative == "." ? current : current / relative;
		if (!unchanged(entry)) return false;
	}
	return true;
}

void copyEntry(fs::path const &source, fs::path const &target, Callbacks const &callbacks, qint64 &bytes, std::vector<Snapshot> &manifest)
{
	check(callbacks);
	Snapshot before = snapshot(source);
#ifdef Q_OS_WIN
	DWORD attributes = GetFileAttributesW(source.c_str());
	if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) && before.type != fs::file_type::symlink) fail(QObject::tr("Unsupported reparse point: %1").arg(text(source)));
#endif
	if (before.type == fs::file_type::symlink) {
		fs::copy_symlink(source, target);
	} else if (before.type == fs::file_type::directory) {
		if (!fs::create_directory(target)) fail(QObject::tr("Cannot create folder: %1").arg(text(target)));
		for (auto const &child : fs::directory_iterator(source)) copyEntry(child.path(), target / child.path().filename(), callbacks, bytes, manifest);
		fs::permissions(target, fs::status(source).permissions());
		fs::last_write_time(target, before.time);
	} else if (before.type == fs::file_type::regular) {
		QFile input(text(source)), output(text(target));
		if (!input.open(QIODevice::ReadOnly)) fail(input.errorString());
		if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly)) fail(output.errorString());
		while (true) {
			check(callbacks);
			QByteArray data = input.read(1024 * 1024);
			if (data.isEmpty()) { if (input.error() != QFileDevice::NoError) fail(input.errorString()); break; }
			if (output.write(data) != data.size()) fail(output.errorString());
			bytes += data.size();
			if (callbacks.progress) callbacks.progress(text(source), bytes);
		}
		if (!output.flush()) fail(output.errorString());
		output.close();
		fs::permissions(target, fs::status(source).permissions());
		fs::last_write_time(target, before.time);
	} else fail(QObject::tr("Unsupported filesystem item: %1").arg(text(source)));
#ifdef Q_OS_WIN
	if (before.type != fs::file_type::symlink) {
		DWORD preserved = attributes & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED);
		if (!SetFileAttributesW(target.c_str(), preserved ? preserved : FILE_ATTRIBUTE_NORMAL)) fail(QObject::tr("Cannot preserve file attributes."));
	}
#endif
	if (!unchanged(before)) fail(QObject::tr("Source changed during transfer: %1").arg(text(source)));
	manifest.push_back(before);
}
}

Results execute(Request request, Callbacks const &callbacks)
{
	Results results;
	QString error = validate(&request);
	if (!error.isEmpty()) { for (auto const &source : request.sources) results.push_back({source, {}, Outcome::Failed, error}); return results; }
	qint64 bytes = 0;
	bool canceled_all = false;
	for (QString const &source : request.sources) {
		ItemResult result{source, QDir(request.destination).filePath(QFileInfo(source).fileName()), Outcome::Failed, {}};
		fs::path container, stage, backup;
		bool installed = false;
		std::vector<Snapshot> destination_manifest;
		try {
			if (canceled_all) throw Canceled{};
			check(callbacks);
			Request current{{source}, request.destination, request.operation};
			error = validate(&current);
			if (!error.isEmpty()) fail(error);
#ifdef Q_OS_WIN
			DWORD attributes = GetFileAttributesW(native(source).c_str());
			if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) && fs::symlink_status(native(source)).type() != fs::file_type::symlink) fail(QObject::tr("Unsupported reparse point."));
#endif
			if (pathKey(current.destination) != pathKey(request.destination)) fail(QObject::tr("Destination changed before transfer."));
			bool replace = false;
			while (entryExists(native(result.target))) {
				std::vector<Snapshot> observed;
				recordTree(native(result.target), observed, callbacks);
				Decision decision = callbacks.conflict ? callbacks.conflict(source, result.target, QFileInfo(result.target).isDir()) : Decision{Choice::Skip, {}};
				check(callbacks);
				if (decision.choice == Choice::Cancel) throw Canceled{};
				if (decision.choice == Choice::Skip) { result.outcome = Outcome::Skipped; break; }
				if (decision.choice == Choice::Replace) { replace = true; destination_manifest = std::move(observed); break; }
				QString name = decision.name;
				if (name.isEmpty() || name == "." || name == ".." || name.contains('/') || name.contains('\\') || name.contains(QChar(0))) fail(QObject::tr("Invalid destination name."));
				result.target = QDir(request.destination).filePath(name);
				if (pathKey(source) == pathKey(result.target)) fail(QObject::tr("Source and destination are the same."));
			}
			if (pathKey(source) == pathKey(result.target) || pathKey(source).startsWith(pathKey(result.target) + '/')) fail(QObject::tr("Cannot replace a source item or its ancestor."));
			if (result.outcome == Outcome::Skipped) { results.push_back(result); continue; }
			if (request.operation == Operation::Move && !replace) {
				bool cross_device = false;
				check(callbacks);
				if (renameExclusive(native(source), native(result.target), &cross_device)) {
					result.outcome = Outcome::Completed;
					results.push_back(result); continue;
				}
				if (!cross_device) fail(QObject::tr("Cannot move item; destination may have changed or access was denied."));
			}
			container = native(temporary(request.destination));
			if (!fs::create_directory(container)) { container.clear(); fail(QObject::tr("Cannot reserve temporary output.")); }
			fs::permissions(container, fs::perms::owner_all);
			stage = container / "payload";
			std::vector<Snapshot> manifest;
			copyEntry(native(source), stage, callbacks, bytes, manifest);
			check(callbacks);
			for (auto const &entry : manifest) if (!unchanged(entry)) fail(QObject::tr("Source changed during transfer."));
			if (replace && entryExists(native(result.target))) {
				if (!unchangedAt(destination_manifest, native(result.target), native(result.target))) fail(QObject::tr("Destination changed after the replacement prompt; it was retained."));
				auto candidate = native(temporary(request.destination));
				if (!renameExclusive(native(result.target), candidate)) fail(QObject::tr("Cannot preserve the existing destination."));
				backup = candidate;
			}
			if (!renameExclusive(stage, native(result.target))) fail(QObject::tr("Cannot install copied item; destination may have changed."));
			installed = true;
			if (request.operation == Operation::Move) {
				for (auto const &entry : manifest) if (!unchanged(entry)) fail(QObject::tr("Copy completed, but source changed; source was retained."));
				fs::remove_all(native(source));
			}
			if (!backup.empty()) {
				if (!unchangedAt(destination_manifest, native(result.target), backup)) fail(QObject::tr("Original destination changed during replacement; backup was retained."));
				fs::remove_all(backup); backup.clear();
			}
			result.outcome = Outcome::Completed;
		} catch (Canceled const &) {
			result.outcome = Outcome::Canceled;
			canceled_all = true;
		} catch (std::exception const &exception) {
			result.error = (installed ? QObject::tr("Destination was installed; source removal or cleanup may be incomplete. ") : QString()) + QString::fromUtf8(exception.what());
		}
		std::error_code cleanup_error;
		if (!container.empty()) fs::remove_all(container, cleanup_error);
		if (cleanup_error) result.error += QObject::tr(" Temporary output remains at %1.").arg(text(stage));
		if (!backup.empty()) {
			if (!installed && renameExclusive(backup, native(result.target))) backup.clear();
			if (!backup.empty()) result.error += QObject::tr(" Original destination retained at %1.").arg(text(backup));
		}
		results.push_back(result);
	}
	return results;
}

struct Job::State {
	std::atomic<bool> canceled{false};
	std::mutex mutex;
	std::condition_variable condition;
	QString source, target, path;
	qint64 bytes = 0;
	bool directory = false, waiting = false, announced = false, answered = false;
	Decision decision;
	Results results;
};
Job::Job(Request request, QObject *parent) : QObject(parent), state_(std::make_shared<State>()), request_(std::move(request))
{
	timer_.setInterval(30);
	connect(&timer_, &QTimer::timeout, this, &Job::poll);
}
Job::~Job() { cancel(); worker_.wait(QDeadlineTimer(0)); }
void Job::start()
{
	if (started_) return;
	started_ = true;
	auto state = state_;
	worker_.start([state, request = request_] {
		Callbacks callbacks;
		callbacks.canceled = [state] { return state->canceled.load(); };
		callbacks.progress = [state](QString const &path, qint64 bytes) { std::lock_guard<std::mutex> lock(state->mutex); state->path = path; state->bytes = bytes; };
		callbacks.conflict = [state](QString const &source, QString const &target, bool directory) {
			std::unique_lock<std::mutex> lock(state->mutex);
			state->source = source; state->target = target; state->directory = directory;
			state->waiting = true; state->announced = false; state->answered = false;
			state->condition.wait(lock, [&] { return state->answered || state->canceled.load(); });
			state->waiting = false;
			return state->canceled ? Decision{} : state->decision;
		};
		auto results = execute(request, callbacks);
		std::lock_guard<std::mutex> lock(state->mutex); state->results = std::move(results);
	});
	timer_.start();
}
void Job::cancel() { std::lock_guard<std::mutex> lock(state_->mutex); state_->canceled = true; state_->condition.notify_all(); }
void Job::resolve(Decision const &decision)
{
	std::lock_guard<std::mutex> lock(state_->mutex);
	state_->decision = decision; state_->answered = true; state_->condition.notify_all();
}
void Job::poll()
{
	QString path, source, target; qint64 bytes; bool ask = false, directory;
	Results results;
	bool finished = !worker_.isRunning();
	{
		std::lock_guard<std::mutex> lock(state_->mutex);
		path = state_->path; bytes = state_->bytes; directory = state_->directory;
		if (state_->waiting && !state_->announced) { state_->announced = true; ask = true; source = state_->source; target = state_->target; }
		if (finished) results = state_->results;
	}
	if (finished) { timer_.stop(); worker_.wait(QDeadlineTimer(0)); emit this->finished(results); return; }
	if (ask) { emit conflict(source, target, directory); return; }
	emit progress(path, bytes);
}
}
