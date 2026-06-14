#ifndef APPLICATIONGLOBAL_H
#define APPLICATIONGLOBAL_H

#include "ApplicationSettings.h"

#include <QColor>
#include <QString>

class QApplication;
class MainWindow;

#ifdef Q_OS_WIN
class WindowsShellAPI;
#endif

struct ApplicationGlobal {
	ApplicationGlobal();

	MainWindow *mainwindow = nullptr;
	bool start_with_shift_key = false;
	QString organization_name;
	QString application_name;
	QString this_executive_program;
	QString language_id;
	QString theme_id;
	QString generic_config_dir;
	QString app_config_dir;
	QString config_file_path;
	QString profiles_xml_path;
	QColor panel_bg_color;

	ApplicationSettings appsettings;

#ifdef Q_OS_WIN
	std::shared_ptr<WindowsShellAPI> shapi;
#endif

	void init(QApplication *a);
};

extern ApplicationGlobal *global;

#define PATH_PREFIX "*"

#endif // APPLICATIONGLOBAL_H
