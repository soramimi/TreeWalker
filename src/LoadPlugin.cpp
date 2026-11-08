#include "LoadPlugin.h"
#include <QMessageBox>

QString PluginLoadError::message() const
{
	const char *stages[] = {"Loading", "Interface", "Creation", "Initialization"};
	QString message = QCoreApplication::translate("PluginLoader", "Plugin: %1\nPath: %2\nStage: %3\nReason: %4")
		.arg(name, path, QCoreApplication::translate("PluginLoader", stages[stage]), reason);
	if (!search_paths.isEmpty()) message += QCoreApplication::translate("PluginLoader", "\nSearch paths: %1").arg(search_paths.join('\n'));
	return message;
}

void showPluginLoadError(PluginLoadError const &error, QWidget *parent)
{
	QMessageBox box(QMessageBox::Critical, QCoreApplication::translate("PluginLoader", "Cannot start TreeWalker"),
		error.message(), QMessageBox::Ok, parent);
	box.setTextFormat(Qt::PlainText);
	box.exec();
}


