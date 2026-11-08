#ifndef LOADPLUGIN_H
#define LOADPLUGIN_H

// #include <Logger.h>
#include <QCoreApplication>
#include <QPluginLoader>
#include <QString>
#include <common/joinpath.h>
#include <memory>
#include <exception>
#include <QDir>

class QWidget;

struct PluginLoadError {
	enum Stage { Loading, InterfaceMismatch, Creation, Initialization };
	QString name;
	QString path;
	QStringList search_paths;
	Stage stage = Loading;
	QString reason;
	QString message() const;
};

void showPluginLoadError(PluginLoadError const &error, QWidget *parent = nullptr);

template <typename ImplType, typename Interface> std::shared_ptr<ImplType> createPluginInstance(QObject *instance, PluginLoadError &error)
{
	auto *plugin = qobject_cast<Interface *>(instance);
	if (!plugin) {
		error.stage = PluginLoadError::InterfaceMismatch;
		error.reason = QCoreApplication::translate("PluginLoader", "The plugin interface is incompatible with this application.");
		return {};
	}
	try {
		error.stage = PluginLoadError::Creation;
		auto ret = std::shared_ptr<ImplType>(plugin->create());
		if (!ret) {
			error.reason = QCoreApplication::translate("PluginLoader", "The plugin did not create an instance.");
			return {};
		}
		error.stage = PluginLoadError::Initialization;
		if (!ret->open()) {
			error.reason = QCoreApplication::translate("PluginLoader", "Plugin initialization failed. Check its resources and dependencies.");
			return {};
		}
		error.reason.clear();
		return ret;
	} catch (std::exception const &exception) {
		error.reason = QString::fromUtf8(exception.what());
	} catch (QString const &message) {
		error.reason = message;
	} catch (...) {
		error.reason = QCoreApplication::translate("PluginLoader", "An unknown plugin error occurred.");
	}
	return {};
}

template <typename ImplType, typename Interface> std::shared_ptr<ImplType> loadPlugin(std::string name, PluginLoadError *failure = nullptr, QString const &explicit_path = {})
{
	std::shared_ptr<ImplType> ret;

#ifdef QT_DEBUG
	name.push_back('d');
#endif
#ifdef Q_OS_WIN
	QString path = QString::fromStdString(name);
#else
	QString path = QCoreApplication::applicationDirPath() / QString::fromStdString("../lib/soramimi.jp/lib" + name + ".so");
#endif
	if (!explicit_path.isEmpty()) path = explicit_path;
	PluginLoadError error;
	error.name = QString::fromStdString(name);
	error.path = path;
	if (QDir::isRelativePath(path)) {
		error.search_paths = QCoreApplication::libraryPaths();
		if (!error.search_paths.contains(QCoreApplication::applicationDirPath())) error.search_paths.prepend(QCoreApplication::applicationDirPath());
	}
	
	char const *const c_name = name.c_str();
	
	// logprintf(LOG_DEFAULT, "loading the plugin %s", path.toStdString().c_str());
	QPluginLoader loader(path);
	auto *instance = loader.instance();
	if (!loader.fileName().isEmpty()) error.path = loader.fileName();
	if (instance) {
		ret = createPluginInstance<ImplType, Interface>(instance, error);
	} else {
		error.reason = loader.errorString();
	}
	if (failure) *failure = error;
	if (ret) qDebug() << QString::asprintf("%s is loaded successfully.", c_name);
	else qWarning().noquote() << error.message();
	
	return ret;
}


#endif // LOADPLUGIN_H
