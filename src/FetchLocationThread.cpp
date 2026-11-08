#include "BasicFileSystemProvider.h"
#include "FetchLocationThread.h"
#include <memory>
#include <algorithm>
#include <QElapsedTimer>
#include <QDebug>

#ifdef Q_OS_WIN
#include "WindowsCom.h"
#include "WindowsShellAPI.h"
#include "WindowsFileSystemProvider.h"
#endif

void FetchLocationThread::run()
{
	if (!data.isDir()) {
		emit failed(data, tr("This location is not a filesystem folder."));
		return;
	}
#ifdef Q_OS_WIN
	WindowsComApartment apartment;
	if (!apartment.initialized()) {
		qWarning() << "Cannot initialize COM for folder loading:" << Qt::hex << static_cast<quint32>(apartment.result());
		emit failed(data, tr("Cannot load this folder."));
		return;
	}
	if (!WindowsShellAPI::forCurrentThread().isReady()) {
		qWarning() << "Cannot initialize Windows Shell for folder loading.";
		emit failed(data, tr("Cannot load this folder."));
		return;
	}
#endif
	QElapsedTimer t;
	t.start();

	FileSystemProviderPtr fs = createFileSystemProvider(data.iidl);

	while (fs->fetch()) {
		if (isInterruptionRequested()) return;
#ifdef Q_OS_WIN
		if (!WindowsComApartment::pumpMessages()) return;
#endif
		FileInfo2 info = fs->fileInfo();
		if (info) {
			if (info.isdir) {
				if (info.name == "." || info.name == "..") {
					continue;
				}
			}
			data.files.push_back(info);
		}
	}
	MainWindow::sortFileInfoList(&data.files);

	int ms = t.elapsed();
	// qDebug() << "FetchLocationThread: fetched " << data.files.size() << " items in " << ms << " ms";
	constexpr int debounce_time_ms = 300;
	while (ms < debounce_time_ms) {
		if (isInterruptionRequested()) return;
#ifdef Q_OS_WIN
		if (!WindowsComApartment::pumpMessages()) return;
#endif
		msleep(std::min(20, debounce_time_ms - ms));
		ms = t.elapsed();
	}
	emit done(data);
}
