#include "darktheme/TraditionalWindowsStyleTreeControl.h"
#include "MainWindow.h"
#include <QApplication>
#include <QProxyStyle>
#include <QKeyEvent>
#include <QDebug>
#include <QStandardPaths>
#include <QPluginLoader>
#include <thread>
#include "common/joinpath.h"
#include "ApplicationGlobal.h"
#include "Theme.h"
#include "../subprojects/FileTypePlugin/src/FileTypeInterface.h"
#include "../subprojects/IncrementalSearchPlugin/src/IncrementalSearchInterface.h"
#include "LoadPlugin.h"

class MyStyle : public QProxyStyle {
private:
	TraditionalWindowsStyleTreeControl legacy_windows_;
public:
	MyStyle()
		: QProxyStyle(0)
	{
	}
	void drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget = 0) const
	{
		if (element == QStyle::PE_IndicatorBranch) {
			if (legacy_windows_.drawPrimitive(element, option, painter, widget)) {
				return;
			}
		}
		QProxyStyle::drawPrimitive(element, option, painter, widget);
	}
};

class MyApplication : public QApplication {
public:
	MyApplication(int &argc, char **argv, int flags = ApplicationFlags)
		: QApplication(argc, argv, flags)
	{
	}
	bool notify(QObject *receiver, QEvent *e)
	{
		if (e->type() == QEvent::KeyPress || e->type() == QEvent::KeyRelease) {
			QKeyEvent *ke = (QKeyEvent *)e;
			auto key = ke->key();
			if (QApplication::activeModalWidget()) {
				// nop:
			} else {
				if (key == Qt::Key_Tab || key == Qt::Key_Period) {
					if (global && global->mainwindow) {
						if (global->mainwindow->acceptKeyEvent(ke)) {
							if (e->type() == QEvent::KeyPress) {
								global->mainwindow->keyPressEvent(ke);
							} else if (e->type() == QEvent::KeyRelease) {
								global->mainwindow->keyReleaseEvent(ke);
							}
							return true;
						}
					}
				}
			}
		}
		return QApplication::notify(receiver, e);
	}
};

ApplicationGlobal *global;

void logHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
	QString message = qFormatLogMessage(type, context, msg);

	if (1) {
#ifdef Q_OS_WIN
		fwprintf(stderr, L"%s\n", (wchar_t *)message.utf16());
#else
		std::string s = message.toStdString();
		fprintf(stderr, "%s\n", s.c_str());
#endif
		fflush(stderr);
	}

	// if (1) {
	// 	logprint(LOG_DEFAULT, s);
	// }
}

void debug()
{
	qDebug() << Q_FUNC_INFO;
	
	auto LoadImage = [](){
		QString path = "/home/soramimi/develop/Guitar/src/resources/Guitar.icns";
		QImage img(path);
		qDebug() << img;
	};
	if (0) { // single thread
		LoadImage();
	} else { // multi thread
		constexpr int N = 1;
		std::vector<std::thread> threads(N);
		for (size_t i = 0; i < N; i++) {
			threads[i] = std::thread([&](){
				LoadImage();
			});
		}
		for (size_t i = 0; i < N; i++) {
			threads[i].join();
		}
	}
}

int main(int argc, char *argv[])
{
	if (0) {
		debug();
	}
	
	putenv("QT_ASSUME_STDERR_HAS_CONSOLE=1");
	// qInstallMessageHandler(logHandler);

	ApplicationGlobal g;
	global = &g;
	
#ifdef Q_OS_WIN
	putenv("QT_ENABLE_HIGHDPI_SCALING=1");
#endif

	MyApplication a(argc, argv);
	global->organization_name = ORGANIZATION_NAME;
	global->application_name = APPLICATION_NAME;
	global->this_executive_program = QFileInfo(argv[0]).absoluteFilePath();
	global->generic_config_dir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
	global->app_config_dir = global->generic_config_dir / global->organization_name / global->application_name;
	global->config_file_path = joinpath(global->app_config_dir, global->application_name + ".ini");

	
	// load plugins
	{
		global->file_type_detector = loadPlugin<FileType, FileTypeInterface>("filetypeplugin"); // file type detector plugin
		global->incremental_search = loadPlugin<IncrementalSearch, IncrementalSearchInterface>("incrementalsearchplugin"); // incremental search plugin
		if (!global->file_type_detector) exit(1);
		if (!global->incremental_search) exit(1);
	}
	
	QPluginLoader loader("darkstyleplugin");

	bool darkstyle = true;
#if 0
	DarkStyleInterface *plugin = dynamic_cast<DarkStyleInterface *>(loader.instance());
	if (plugin) {
		if (darkstyle) {
			plugin->applyDarkStyle(&a);
		} else {
			plugin->applyLightStyle(&a);
		}
	}
#else
	if (darkstyle) {
		auto theme = createDarkTheme();
		a.setStyle(theme->newStyle());
		a.setPalette(a.style()->standardPalette());
	} else {
		auto theme = createLightTheme();
		a.setStyle(theme->newStyle());
#ifndef Q_OS_WIN
		a.setPalette(a.style()->standardPalette());
#endif
	}
#endif

	qRegisterMetaType<LocationData>("LocationData");
	qRegisterMetaType<ItemIdList>("ItemIdList");


	MainWindow w;
	global->mainwindow = &w;
	w.show();
	w.reloadContents();

	int r = a.exec();

	return r;
}
