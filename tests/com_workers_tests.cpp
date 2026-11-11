#include "ApplicationGlobal.h"
#include "FetchLocationThread.h"
#include "ThumbnailLoader.h"
#include "WindowsCom.h"
#include "WindowsFileSystemProvider.h"
#include "WindowsShellAPI.h"

#include <QDir>
#include <QFile>
#include <QElapsedTimer>
#include <QThread>
#include <QTemporaryDir>
#include <QtTest>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

namespace {

HRESULT apartmentType(APTTYPE *type = nullptr, APTTYPEQUALIFIER *type_qualifier = nullptr)
{
	APTTYPE actual_type = APTTYPE_CURRENT;
	APTTYPEQUALIFIER qualifier = APTTYPEQUALIFIER_NONE;
	HRESULT result = CoGetApartmentType(&actual_type, &qualifier);
	if (type) *type = actual_type;
	if (type_qualifier) *type_qualifier = qualifier;
	return result;
}

bool hasNoExplicitApartment(HRESULT result, APTTYPE type, APTTYPEQUALIFIER qualifier)
{
	return result == CO_E_NOTINITIALIZED
		|| (result == S_OK && type == APTTYPE_MTA && qualifier == APTTYPEQUALIFIER_IMPLICIT_MTA);
}

// A thread with no explicit COM references can initialize either mode with
// S_OK. Probe the opposite mode so a leaked reference fails instead of being
// obscured by the process's implicit MTA. Balance the probe independently.
HRESULT probeApartment(DWORD flags)
{
	HRESULT result = CoInitializeEx(nullptr, flags | COINIT_DISABLE_OLE1DDE);
	if (SUCCEEDED(result)) CoUninitialize();
	return result;
}

// Construct before the thread-local Shell API so this observer runs after its
// destructor and verifies that the Shell API balances its own COM reference.
struct ApartmentExitProbe {
	HRESULT *result;
	DWORD flags = COINIT_MULTITHREADED;
	~ApartmentExitProbe() { *result = probeApartment(flags); }
};

struct FetchProbe {
	HRESULT before = E_UNEXPECTED;
	APTTYPE before_type = APTTYPE_CURRENT;
	APTTYPEQUALIFIER before_qualifier = APTTYPEQUALIFIER_NONE;
	HRESULT during = E_UNEXPECTED;
	APTTYPE during_type = APTTYPE_CURRENT;
	HRESULT after_thread_locals = E_UNEXPECTED;
};

struct ExitCallback {
	std::function<void()> callback;
	~ExitCallback() { callback(); }
};

struct IdleTimerProbe {
	std::atomic<bool> scheduled{false};
	std::atomic<bool> fired{false};
	UINT_PTR timer = 0;
	DWORD scheduling_thread = 0;
	DWORD callback_thread = 0;
	HRESULT apartment = E_UNEXPECTED;
	APTTYPE type = APTTYPE_CURRENT;
};

// The worker retains this shared state until its callback or thread exit, so
// failure/timeout in the GUI test cannot leave a dangling timer reference.
thread_local std::shared_ptr<IdleTimerProbe> idle_timer_probe;

void CALLBACK idleTimerCallback(HWND, UINT, UINT_PTR timer, DWORD)
{
	KillTimer(nullptr, timer);
	auto probe = std::move(idle_timer_probe);
	if (!probe) return;
	probe->callback_thread = GetCurrentThreadId();
	probe->apartment = apartmentType(&probe->type);
	probe->fired.store(true, std::memory_order_release);
}

} // namespace

class ComWorkerTests : public QObject {
	Q_OBJECT
private slots:
	void shellFileInfoPreservesHiddenAttribute()
	{
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		for (bool folder : {false, true}) {
			const QString path = directory.filePath(folder ? "hidden-folder" : "hidden-file.txt");
			if (folder) QVERIFY(QDir().mkdir(path));
			else { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); }
			QVERIFY(SetFileAttributesW(reinterpret_cast<LPCWSTR>(path.utf16()), FILE_ATTRIBUTE_HIDDEN));
			const auto identity = WindowsShellAPI::forCurrentThread().parseDisplayName(path);
			QVERIFY(!identity.empty());
			auto info = WindowsFileSystemProvider::getFileInfo(nullptr, identity);
			QVERIFY(info);
			QVERIFY(info.ishidden);
			QCOMPARE(info.isdir, folder);
			QVERIFY(SetFileAttributesW(reinterpret_cast<LPCWSTR>(path.utf16()), FILE_ATTRIBUTE_NORMAL));
		}
	}

	void nestedInitializationIsBalanced()
	{
		struct Result {
			HRESULT before = E_UNEXPECTED;
			APTTYPE before_type = APTTYPE_CURRENT;
			APTTYPEQUALIFIER before_qualifier = APTTYPEQUALIFIER_NONE;
			HRESULT outer = E_UNEXPECTED;
			HRESULT nested = E_UNEXPECTED;
			HRESULT after_nested = E_UNEXPECTED;
			HRESULT after_outer = E_UNEXPECTED;
			APTTYPE type = APTTYPE_CURRENT;
		} result;
		std::thread worker([&] {
			result.before = apartmentType(&result.before_type, &result.before_qualifier);
			{
				WindowsComApartment outer;
				result.outer = outer.result();
				{
					WindowsComApartment nested;
					result.nested = nested.result();
				}
				result.after_nested = apartmentType(&result.type);
			}
			result.after_outer = probeApartment(COINIT_MULTITHREADED);
		});
		worker.join();
		QVERIFY(hasNoExplicitApartment(result.before, result.before_type, result.before_qualifier));
		QCOMPARE(result.outer, HRESULT(S_OK));
		QCOMPARE(result.nested, HRESULT(S_FALSE));
		QCOMPARE(result.after_nested, HRESULT(S_OK));
		QCOMPARE(result.type, APTTYPE_STA);
		QCOMPARE(result.after_outer, HRESULT(S_OK));
	}

	void incompatibleInitializationDoesNotUninitializeOuterApartment()
	{
		struct Result {
			HRESULT outer = E_UNEXPECTED;
			HRESULT incompatible = E_UNEXPECTED;
			bool incompatible_initialized = true;
			HRESULT after_failure = E_UNEXPECTED;
			HRESULT after_outer = E_UNEXPECTED;
			APTTYPE type = APTTYPE_CURRENT;
		} result;
		std::thread worker([&] {
			{
				WindowsComApartment outer(COINIT_MULTITHREADED | COINIT_DISABLE_OLE1DDE);
				result.outer = outer.result();
				{
					WindowsComApartment incompatible;
					result.incompatible = incompatible.result();
					result.incompatible_initialized = incompatible.initialized();
				}
				result.after_failure = apartmentType(&result.type);
			}
			result.after_outer = probeApartment(COINIT_APARTMENTTHREADED);
		});
		worker.join();
		QCOMPARE(result.outer, HRESULT(S_OK));
		QCOMPARE(result.incompatible, HRESULT(RPC_E_CHANGED_MODE));
		QVERIFY(!result.incompatible_initialized);
		QCOMPARE(result.after_failure, HRESULT(S_OK));
		QCOMPARE(result.type, APTTYPE_MTA);
		QCOMPARE(result.after_outer, HRESULT(S_OK));
	}

	void shellApisAreLocalToTheirThreads()
	{
		struct Result {
			uintptr_t address = 0;
			bool ready = false;
			bool same_instance = false;
			bool parsed_directory = false;
			HRESULT initialized = E_UNEXPECTED;
			HRESULT after_thread_locals = E_UNEXPECTED;
			APTTYPE type = APTTYPE_CURRENT;
		};
		std::array<Result, 2> results;
		std::mutex mutex;
		std::condition_variable cv;
		int arrived = 0;
		bool release = false;
		const QString root_path = QDir::rootPath();
		std::array<std::thread, 2> workers;
		for (size_t index = 0; index < workers.size(); ++index) {
			workers[index] = std::thread([&, index] {
				thread_local ApartmentExitProbe exit_probe{&results[index].after_thread_locals,
					index == 0 ? COINIT_MULTITHREADED : COINIT_APARTMENTTHREADED};
				const DWORD flags = index == 0 ? COINIT_APARTMENTTHREADED : COINIT_MULTITHREADED;
				WindowsComApartment apartment(flags | COINIT_DISABLE_OLE1DDE);
				results[index].initialized = apartment.result();
				auto &shell = WindowsShellAPI::forCurrentThread();
				results[index].address = reinterpret_cast<uintptr_t>(&shell);
				results[index].ready = shell.isReady();
				results[index].same_instance = &shell == &WindowsShellAPI::forCurrentThread();
				results[index].parsed_directory = !shell.parseDisplayName(root_path).isEmpty();
				apartmentType(&results[index].type);
				std::unique_lock<std::mutex> lock(mutex);
				++arrived;
				cv.notify_all();
				cv.wait_for(lock, std::chrono::seconds(5), [&] { return release; });
			});
		}
		bool both_arrived;
		{
			std::unique_lock<std::mutex> lock(mutex);
			both_arrived = cv.wait_for(lock, std::chrono::seconds(5), [&] { return arrived == 2; });
			release = true;
		}
		cv.notify_all();
		for (auto &worker : workers) worker.join();
		QVERIFY(both_arrived);
		QVERIFY(results[0].address != results[1].address);
		for (Result const &result : results) {
			QCOMPARE(result.initialized, HRESULT(S_OK));
			QVERIFY(result.ready);
			QVERIFY(result.same_instance);
			QVERIFY(result.parsed_directory);
			QCOMPARE(result.after_thread_locals, HRESULT(S_OK));
		}
		QCOMPARE(results[0].type, APTTYPE_STA);
		QCOMPARE(results[1].type, APTTYPE_MTA);
	}

	void implicitMtaDoesNotChooseShellWorkerApartment()
	{
		std::mutex mutex;
		std::condition_variable cv;
		bool holder_ready = false;
		bool release_holder = false;
		HRESULT holder_initialized = E_UNEXPECTED;
		std::thread holder([&] {
			WindowsComApartment apartment(COINIT_MULTITHREADED | COINIT_DISABLE_OLE1DDE);
			std::unique_lock<std::mutex> lock(mutex);
			holder_initialized = apartment.result();
			holder_ready = true;
			cv.notify_all();
			cv.wait_for(lock, std::chrono::seconds(5), [&] { return release_holder; });
		});
		bool ready;
		{
			std::unique_lock<std::mutex> lock(mutex);
			ready = cv.wait_for(lock, std::chrono::seconds(5), [&] { return holder_ready; });
		}
		HRESULT before = E_UNEXPECTED;
		APTTYPE before_type = APTTYPE_CURRENT;
		APTTYPEQUALIFIER before_qualifier = APTTYPEQUALIFIER_NONE;
		HRESULT during = E_UNEXPECTED;
		APTTYPE during_type = APTTYPE_CURRENT;
		HRESULT after_thread_locals = E_UNEXPECTED;
		bool shell_ready = false;
		std::thread worker([&] {
			thread_local ApartmentExitProbe exit_probe{&after_thread_locals};
			before = apartmentType(&before_type, &before_qualifier);
			auto &shell = WindowsShellAPI::forCurrentThread();
			shell_ready = shell.isReady();
			during = apartmentType(&during_type);
		});
		worker.join();
		{
			std::lock_guard<std::mutex> lock(mutex);
			release_holder = true;
		}
		cv.notify_all();
		holder.join();
		QVERIFY(ready);
		QCOMPARE(holder_initialized, HRESULT(S_OK));
		QCOMPARE(before, HRESULT(S_OK));
		QCOMPARE(before_type, APTTYPE_MTA);
		QCOMPARE(before_qualifier, APTTYPEQUALIFIER_IMPLICIT_MTA);
		QVERIFY(shell_ready);
		QCOMPARE(during, HRESULT(S_OK));
		QCOMPARE(during_type, APTTYPE_STA);
		QCOMPARE(after_thread_locals, HRESULT(S_OK));
	}

	void shellEnumerationWorkersInitializeCom()
	{
		const ItemIdList drives = FileItem::getDrives().idlist();
		QVERIFY(!drives.empty());
		for (int iteration = 0; iteration < 3; ++iteration) {
			auto probe = std::make_shared<FetchProbe>();
			FetchLocationThread worker([probe](LocationData data, auto const &stop) {
				thread_local ExitCallback exit{[probe] { probe->after_thread_locals = probeApartment(COINIT_MULTITHREADED); }};
				probe->before = apartmentType(&probe->before_type, &probe->before_qualifier);
				auto result = FetchLocationThread::fetch(std::move(data), stop);
				probe->during = apartmentType(&probe->during_type);
				return result;
			});
			worker.data.kind = Kind::Directory;
			worker.data.iidl = drives;
			worker.data.path = "//itemidlist//" + QString::fromLatin1(drives.iidl().toHex());
			int notifications = 0;
			connect(&worker, &FetchLocationThread::done, &worker, [&](LocationData const &) {
				QCOMPARE(QThread::currentThread(), qApp->thread());
				++notifications;
			});
			worker.start();
			QVERIFY2(worker.wait(QDeadlineTimer(5000)), "Shell enumeration did not finish within five seconds");
			QVERIFY(hasNoExplicitApartment(probe->before, probe->before_type, probe->before_qualifier));
			QCOMPARE(notifications, 1);
			QCOMPARE(probe->during, HRESULT(S_OK));
			QCOMPARE(probe->during_type, APTTYPE_STA);
			QVERIFY(!worker.data.files.empty());
			for (auto const &info : worker.data.files) QVERIFY(info.icon.isNull());
			QCOMPARE(probe->after_thread_locals, HRESULT(S_OK));
		}
	}

	void enumerationInitializationFailurePreservesOuterApartment()
	{
		struct Probe {
			HRESULT initialized = E_UNEXPECTED;
			HRESULT after_failure = E_UNEXPECTED;
			HRESULT after_outer = E_UNEXPECTED;
			APTTYPE type = APTTYPE_CURRENT;
		};
		auto probe = std::make_shared<Probe>();
		FetchLocationThread worker([probe](LocationData data, auto const &stop) -> LocationData {
			QString error;
			{
				WindowsComApartment outer(COINIT_MULTITHREADED | COINIT_DISABLE_OLE1DDE);
				probe->initialized = outer.result();
				try { FetchLocationThread::fetch(std::move(data), stop); }
				catch (QString const &message) { error = message; }
				probe->after_failure = apartmentType(&probe->type);
			}
			probe->after_outer = probeApartment(COINIT_APARTMENTTHREADED);
			throw error;
		});
		worker.data.kind = Kind::Directory;
		worker.data.iidl = FileItem::getDrives().idlist();
		worker.data.path = "//itemidlist//" + QString::fromLatin1(worker.data.iidl.iidl().toHex());
		int failures = 0;
		int completions = 0;
		QString error;
		connect(&worker, &FetchLocationThread::failed, &worker, [&](LocationData const &, QString const &message) {
			QCOMPARE(QThread::currentThread(), qApp->thread());
			++failures;
			error = message;
		});
		connect(&worker, &FetchLocationThread::done, &worker, [&](LocationData const &) { ++completions; });
		worker.start();
		QVERIFY2(worker.wait(QDeadlineTimer(5000)), "COM initialization failure did not finish promptly");
		QCOMPARE(probe->initialized, HRESULT(S_OK));
		QCOMPARE(failures, 1);
		QCOMPARE(completions, 0);
		QVERIFY(!error.isEmpty());
		QCOMPARE(probe->after_failure, HRESULT(S_OK));
		QCOMPARE(probe->type, APTTYPE_MTA);
		QCOMPARE(probe->after_outer, HRESULT(S_OK));
	}

	void thumbnailWorkersInitializeCom()
	{
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		QImage source(48, 24, QImage::Format_ARGB32);
		source.fill(Qt::red);
		for (int iteration = 0; iteration < 3; ++iteration) {
			const QString path = directory.filePath(QString("thumbnail-%1.bmp").arg(iteration));
			QVERIFY(source.save(path, "BMP"));
			auto probe = std::make_shared<FetchProbe>();
			ThumbnailLoader loader([probe](QString const &path) {
				probe->during = apartmentType(&probe->during_type);
				return WindowsShellAPI::queryThumbnail(path);
			});
			std::atomic<int> notifications{0};
			QImage image;
			connect(&loader, &ThumbnailLoader::taskDone, &loader, [&](std::shared_ptr<ThumbnailLoader::Entiry> entity) {
				QCOMPARE(QThread::currentThread(), qApp->thread());
				image = entity->image;
				notifications.fetch_add(1, std::memory_order_release);
			}, Qt::DirectConnection);
			loader.start();
			QVERIFY(!loader.query(path));
			QTRY_VERIFY_WITH_TIMEOUT(notifications.load(std::memory_order_acquire) > 0, 5000);
			loader.stop();
			QCOMPARE(probe->during, HRESULT(S_OK));
			QCOMPARE(probe->during_type, APTTYPE_STA);
			QVERIFY(!image.isNull());
			QVERIFY(image.width() <= 128 && image.height() <= 128);
			auto cached = loader.query(path);
			QVERIFY(cached);
			QCOMPARE(cached->image, image);
		}
	}

	void thumbnailWorkersDispatchStaMessagesWhileIdle()
	{
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const QString path = directory.filePath("idle-timer.bmp");
		QImage image(32, 32, QImage::Format_ARGB32);
		image.fill(Qt::blue);
		QVERIFY(image.save(path, "BMP"));
		auto probe = std::make_shared<IdleTimerProbe>();
		ThumbnailLoader loader([probe](QString const &path) {
			probe->scheduling_thread = GetCurrentThreadId();
			idle_timer_probe = probe;
			probe->timer = SetTimer(nullptr, 0, 20, idleTimerCallback);
			probe->scheduled.store(true, std::memory_order_release);
			return WindowsShellAPI::queryThumbnail(path);
		});
		loader.start();
		QVERIFY(!loader.query(path));
		QElapsedTimer timeout;
		timeout.start();
		while (!probe->fired.load(std::memory_order_acquire) && timeout.elapsed() < 5000) QTest::qWait(10);
		loader.stop();
		QVERIFY(probe->scheduled.load(std::memory_order_acquire));
		QVERIFY2(probe->timer != 0, "SetTimer failed on the thumbnail worker");
		QVERIFY2(probe->fired.load(std::memory_order_acquire), "The idle thumbnail worker did not dispatch its STA timer callback");
		QCOMPARE(probe->callback_thread, probe->scheduling_thread);
		QVERIFY(probe->callback_thread != GetCurrentThreadId());
		QCOMPARE(probe->apartment, HRESULT(S_OK));
		QCOMPARE(probe->type, APTTYPE_STA);
	}

	void shellFailuresLeaveApartmentUsable()
	{
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const QString missing = directory.filePath("missing-directory/missing.bmp");
		bool ready = false;
		bool rejected_path = false;
		bool rejected_thumbnail = false;
		bool still_usable = false;
		HRESULT after_failure = E_UNEXPECTED;
		std::thread worker([&] {
			WindowsComApartment apartment;
			auto &shell = WindowsShellAPI::forCurrentThread();
			ready = apartment.initialized() && shell.isReady();
			rejected_path = shell.parseDisplayName(missing).isEmpty();
			rejected_thumbnail = WindowsShellAPI::queryThumbnail(missing).isNull();
			after_failure = apartmentType();
			still_usable = !shell.parseDisplayName(QDir::rootPath()).isEmpty();
		});
		worker.join();
		QVERIFY(ready);
		QVERIFY(rejected_path);
		QVERIFY(rejected_thumbnail);
		QCOMPARE(after_failure, HRESULT(S_OK));
		QVERIFY(still_usable);
	}
};

int runComWorkerTests(int argc, char **argv)
{
	ComWorkerTests tests;
	return QTest::qExec(&tests, argc, argv);
}

#include "com_workers_tests.moc"
