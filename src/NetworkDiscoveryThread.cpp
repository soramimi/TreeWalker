#include "NetworkDiscoveryThread.h"
#include "WindowsShellAPI.h"
#include "WindowsCom.h"

#include <QHash>
#include <QPixmap>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shobjidl.h>

namespace {

struct ReleaseCom {
	void operator()(IUnknown *object) const { if (object) object->Release(); }
};

struct FreePidl {
	void operator()(ITEMIDLIST *pidl) const { CoTaskMemFree(pidl); }
};

template <typename T> using ComPtr = std::unique_ptr<T, ReleaseCom>;
using PidlPtr = std::unique_ptr<ITEMIDLIST, FreePidl>;

// This enumerator owns all of its Shell resources. It must not use global or a
// QObject: a slow Shell extension may return after the window has been destroyed.
bool enumerateNetwork(NetworkDiscoveryThread::Visitor const &visit)
{
	WindowsComApartment apartment;
	if (!apartment.initialized()) return false;

	ITEMIDLIST *network_raw = nullptr;
	HRESULT hr = SHGetSpecialFolderLocation(nullptr, CSIDL_NETWORK, &network_raw);
	PidlPtr network(network_raw);
	if (FAILED(hr) || !network) return false;

	IShellFolder *desktop_raw = nullptr;
	hr = SHGetDesktopFolder(&desktop_raw);
	ComPtr<IShellFolder> desktop(desktop_raw);
	if (FAILED(hr) || !desktop) return false;

	IShellFolder *folder_raw = nullptr;
	hr = desktop->BindToObject(network.get(), nullptr, IID_PPV_ARGS(&folder_raw));
	ComPtr<IShellFolder> folder(folder_raw);
	if (FAILED(hr) || !folder) return false;

	IEnumIDList *items_raw = nullptr;
	hr = folder->EnumObjects(nullptr, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS | SHCONTF_INCLUDEHIDDEN, &items_raw);
	ComPtr<IEnumIDList> items(items_raw);
	if (hr == S_FALSE) return true;
	if (FAILED(hr) || !items) return false;

	QHash<int, QImage> icon_cache;
	while (true) {
		if (!WindowsComApartment::pumpMessages()) return false;
		ITEMIDLIST *child_raw = nullptr;
		ULONG fetched = 0;
		hr = items->Next(1, &child_raw, &fetched);
		PidlPtr child(child_raw);
		if (hr == S_FALSE) return true;
		if (FAILED(hr) || fetched != 1 || !child) return false;

		PidlPtr absolute(ILCombine(network.get(), child.get()));
		if (!absolute) return false;

		SHFILEINFOW shell_info{};
		HIMAGELIST images = reinterpret_cast<HIMAGELIST>(SHGetFileInfoW(
			reinterpret_cast<wchar_t const *>(absolute.get()), 0, &shell_info, sizeof(shell_info),
			SHGFI_PIDL | SHGFI_DISPLAYNAME | SHGFI_ATTRIBUTES | SHGFI_SYSICONINDEX));
		if (!images) continue;

		NetworkDiscoveryThread::Entry entry;
		wchar_t path[MAX_PATH]{};
		if (SHGetPathFromIDListW(absolute.get(), path)) entry.info.path = QString::fromWCharArray(path);
		if (entry.info.path.isEmpty() && !(shell_info.dwAttributes & SFGAO_FOLDER)) continue;
		entry.info.iidl = ItemIdList(QByteArray(reinterpret_cast<char const *>(absolute.get()), ILGetSize(absolute.get())));
		entry.info.name = QString::fromWCharArray(shell_info.szDisplayName);
		entry.info.isdir = entry.info.path.isEmpty() || (shell_info.dwAttributes & SFGAO_FOLDER);
		entry.info.ishidden = (shell_info.dwAttributes & SFGAO_HIDDEN) != 0;

		auto icon = icon_cache.find(shell_info.iIcon);
		if (icon == icon_cache.end()) {
			QImage image;
			HICON handle = ImageList_GetIcon(images, shell_info.iIcon, ILD_NORMAL);
			if (handle) {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
				ICONINFO info{};
				if (GetIconInfo(handle, &info)) {
					if (info.hbmColor) image = WindowsShellAPI::convertHBitmapToQImage(info.hbmColor);
					if (info.hbmColor) DeleteObject(info.hbmColor);
					if (info.hbmMask) DeleteObject(info.hbmMask);
				}
#else
				image = QImage::fromHICON(handle);
#endif
				DestroyIcon(handle);
			}
			icon = icon_cache.insert(shell_info.iIcon, image);
		}
		entry.icon = icon.value();
		if (!visit(entry)) return false;
	}
}

} // namespace

struct NetworkDiscoveryThread::State {
	std::mutex mutex;
	std::condition_variable cv;
	bool interrupted = false;
	bool refresh_requested = false;
	bool ready = false;
	uint64_t revision = 0;
	std::vector<Entry> files;
};

NetworkDiscoveryThread::NetworkDiscoveryThread(Enumerator enumerate)
	: state_(std::make_shared<State>())
	, enumerator_(enumerate ? std::move(enumerate) : Enumerator(enumerateNetwork))
{
	notification_timer_.setInterval(200);
	connect(&notification_timer_, &QTimer::timeout, this, [this] {
		uint64_t revision;
		{
			std::lock_guard<std::mutex> lock(state_->mutex);
			revision = state_->revision;
		}
		if (revision != delivered_revision_) {
			delivered_revision_ = revision;
			emit filesChanged();
		}
	});
}

NetworkDiscoveryThread::~NetworkDiscoveryThread()
{
	stop();
}

void NetworkDiscoveryThread::start()
{
	if (started_) return;
	state_ = std::make_shared<State>();
	delivered_revision_ = 0;
	auto state = state_;
	auto enumerate = enumerator_;
	thread_.start([state, enumerate] { run(state, enumerate); });
	started_ = true;
	notification_timer_.start();
}

void NetworkDiscoveryThread::run(std::shared_ptr<State> state, Enumerator enumerate)
{
	using namespace std::chrono;
	constexpr seconds min_interval(5);
	constexpr seconds max_interval(60);
	seconds interval = min_interval;
	try {
		while (true) {
			{
				std::lock_guard<std::mutex> lock(state->mutex);
				if (state->interrupted) break;
			}
			std::vector<ItemIdList> seen;
			bool changed = false;
			const bool completed = enumerate([&](Entry const &entry) {
				std::lock_guard<std::mutex> lock(state->mutex);
				if (state->interrupted) return false;
				seen.push_back(entry.info.iidl);
				auto existing = std::find_if(state->files.begin(), state->files.end(), [&](Entry const &file) {
					return file.info.iidl == entry.info.iidl;
				});
				if (existing == state->files.end()) {
					state->files.push_back(entry);
					changed = true;
					++state->revision;
				} else if (existing->info.name != entry.info.name || existing->info.path != entry.info.path
					|| existing->info.isdir != entry.info.isdir || existing->info.ishidden != entry.info.ishidden
					|| existing->icon != entry.icon) {
					*existing = entry;
					changed = true;
					++state->revision;
				}
				return true;
			});

			std::unique_lock<std::mutex> lock(state->mutex);
			if (state->interrupted) break;
			if (!state->ready) {
				state->ready = true;
				++state->revision;
			}
			if (completed) {
				auto removed = std::remove_if(state->files.begin(), state->files.end(), [&](Entry const &entry) {
					return std::find(seen.begin(), seen.end(), entry.info.iidl) == seen.end();
				});
				if (removed != state->files.end()) {
					state->files.erase(removed, state->files.end());
					changed = true;
					++state->revision;
				}
			}
			if (changed) interval = min_interval;
			state->cv.wait_for(lock, interval, [&] { return state->interrupted || state->refresh_requested; });
			if (state->interrupted) break;
			if (state->refresh_requested) {
				state->refresh_requested = false;
				interval = min_interval;
			} else if (!changed) {
				interval = std::min(interval * 2, max_interval);
			}
		}
	} catch (...) {
		// A failed backend must not terminate the process or leave the UI waiting.
		std::lock_guard<std::mutex> lock(state->mutex);
		if (!state->interrupted) {
			state->ready = true;
			++state->revision;
		}
	}
}

bool NetworkDiscoveryThread::isReady() const
{
	std::lock_guard<std::mutex> lock(state_->mutex);
	return state_->ready;
}

void NetworkDiscoveryThread::refresh()
{
	{
		std::lock_guard<std::mutex> lock(state_->mutex);
		state_->refresh_requested = true;
	}
	state_->cv.notify_all();
}

void NetworkDiscoveryThread::stop()
{
	notification_timer_.stop();
	{
		std::lock_guard<std::mutex> lock(state_->mutex);
		state_->interrupted = true;
	}
	state_->cv.notify_all();
	// Completion includes TLS/COM cleanup; a flag set just before returning
	// from run() would still permit an unbounded join during TLS destruction.
	thread_.wait(QDeadlineTimer(0));
	started_ = false;
}

std::vector<FileInfo2> NetworkDiscoveryThread::files() const
{
	std::vector<Entry> entries;
	{
		std::lock_guard<std::mutex> lock(state_->mutex);
		entries = state_->files;
	}
	std::vector<FileInfo2> files;
	files.reserve(entries.size());
	for (Entry const &entry : entries) {
		FileInfo2 info = entry.info;
		if (!entry.icon.isNull()) info.icon = QIcon(QPixmap::fromImage(entry.icon));
		files.push_back(std::move(info));
	}
	return files;
}
