#ifndef APPLICATIONGLOBAL_H
#define APPLICATIONGLOBAL_H

#include "ApplicationSettings.h"

#include <QColor>
#include <QImage>
#include <QString>

#include <subprojects/IncrementalSearchPlugin/src/IncrementalSearch.h>

#include <subprojects/FileTypePlugin/src/FileType.h>

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

	QImage folder_icon;
	QImage zip_file_icon;

	ApplicationSettings appsettings;

#ifdef Q_OS_WIN
	std::shared_ptr<WindowsShellAPI> shapi;
#endif
	
	std::shared_ptr<IncrementalSearch> incremental_search;
	IncrementalSearchFilter makeIncrementalSearchFilter(const std::string &filtertext);
	QString incremental_search_text;
	
	std::shared_ptr<FileType> file_type_detector;
	std::string mimetype_by_data(const char *data, size_t size);
	std::string mimetype_by_data(const QByteArray &ba);
	std::string mimetype_by_data(std::vector<char> const &ba);
	std::string mimetype_by_file(const char *path);
	std::string mimetype_by_file(std::string const &path);
	
	bool is_extension_archive_file(QString const &suffix);
	QIcon makeArchiveFileIcon(const QString &suffix);
};

extern ApplicationGlobal *global;

#define PATH_PREFIX "*"

#endif // APPLICATIONGLOBAL_H
