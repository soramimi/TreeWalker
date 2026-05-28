#include "BasicFileSystemProvider.h"
#include "FetchLocationThread.h"
#include <memory>
#include <QElapsedTimer>
#include <QDebug>
#include "ApplicationGlobal.h"

#ifdef Q_OS_WIN
#include "WindowsShellAPI.h"
#include "WindowsFileSystemProvider.h"
#endif

void FetchLocationThread::run()
{
	QElapsedTimer t;
	t.start();

	FileSystemProviderPtr fs = global->mainwindow->newFileSystemPtr(data.iidl);

	while (fs->fetch()) {
		if (isInterruptionRequested()) return;
		FileInfo2 info = fs->fileInfo();
		if (info) {
			if (info.isdir) {
				if (info.name == "." || info.name == "..") {
					continue;
				}
			}
			data.files.append(info);
		}
	}
	MainWindow::sortFileInfoList(&data.files);

	int ms = t.elapsed();
	// qDebug() << "FetchLocationThread: fetched " << data.files.size() << " items in " << ms << " ms";
	constexpr int debounce_time_ms = 300;
	if (ms < debounce_time_ms) {
		msleep(debounce_time_ms - ms);
	}
	emit done(data);
}
