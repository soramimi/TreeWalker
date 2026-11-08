#include "LoadPlugin.h"
#include "subprojects/FileTypePlugin/src/FileTypeInterface.h"
#include <QApplication>
#include <QFile>
#include <QMessageBox>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <iostream>

struct PluginCounters { int opened = 0; int destroyed = 0; };

struct TestPluginInstance {
	PluginCounters *counters;
	bool succeeds;
	~TestPluginInstance() { ++counters->destroyed; }
	bool open() { ++counters->opened; return succeeds; }
};

class TestPluginInterface {
public:
	virtual ~TestPluginInterface() = default;
	virtual TestPluginInstance *create() = 0;
};
Q_DECLARE_INTERFACE(TestPluginInterface, "TreeWalker.PluginLoaderTests/1.0")

class TestPluginFactory : public QObject, public TestPluginInterface {
	Q_OBJECT
	Q_INTERFACES(TestPluginInterface)
public:
	PluginCounters counters;
	bool creates = true;
	bool opens = true;
	TestPluginInstance *create() override { return creates ? new TestPluginInstance{&counters, opens} : nullptr; }
};

class PluginLoadingTests : public QObject {
	Q_OBJECT
private slots:
	void actualFileTypePluginLoads()
	{
		PluginLoadError error;
		auto plugin = loadPlugin<FileType, FileTypeInterface>("filetypeplugin", &error);
		QVERIFY2(plugin, qPrintable(error.message()));
		QVERIFY(error.reason.isEmpty());
	}

	void missingAndInvalidFiles_data()
	{
		QTest::addColumn<bool>("exists");
		QTest::newRow("missing") << false;
		QTest::newRow("invalid library") << true;
	}
	void missingAndInvalidFiles()
	{
		QFETCH(bool, exists);
		QTemporaryDir files;
		QVERIFY(files.isValid());
		QString path = files.filePath("test-plugin.so");
		if (exists) {
			QFile file(path);
			QVERIFY(file.open(QIODevice::WriteOnly));
			file.write("not a plugin");
		}
		PluginLoadError error;
		auto plugin = loadPlugin<TestPluginInstance, TestPluginInterface>("test-plugin", &error, path);
		QVERIFY(!plugin);
		QCOMPARE(error.path, path);
		QCOMPARE(error.stage, PluginLoadError::Loading);
		QVERIFY(!error.name.isEmpty());
		QVERIFY(!error.reason.isEmpty());
		QVERIFY(error.message().contains(path));
	}
	void incompatibleInterface()
	{
		QObject instance;
		PluginLoadError error;
		QVERIFY((!createPluginInstance<TestPluginInstance, TestPluginInterface>(&instance, error)));
		QCOMPARE(error.stage, PluginLoadError::InterfaceMismatch);
		QVERIFY(!error.reason.isEmpty());
	}
	void creationAndInitialization_data()
	{
		QTest::addColumn<bool>("creates");
		QTest::addColumn<bool>("opens");
		QTest::newRow("null instance") << false << false;
		QTest::newRow("initialization failure") << true << false;
		QTest::newRow("success") << true << true;
	}
	void creationAndInitialization()
	{
		QFETCH(bool, creates);
		QFETCH(bool, opens);
		TestPluginFactory factory;
		factory.creates = creates;
		factory.opens = opens;
		PluginLoadError error;
		auto result = createPluginInstance<TestPluginInstance, TestPluginInterface>(&factory, error);
		QCOMPARE(bool(result), creates && opens);
		QCOMPARE(factory.counters.opened, creates ? 1 : 0);
		if (!creates) QCOMPARE(error.stage, PluginLoadError::Creation);
		else QCOMPARE(error.stage, PluginLoadError::Initialization);
		if (result) {
			QVERIFY(error.reason.isEmpty());
			QVERIFY(!QApplication::activeModalWidget());
			result.reset();
		} else QVERIFY(!error.reason.isEmpty());
		QCOMPARE(factory.counters.destroyed, creates ? 1 : 0);
	}
	void failureDialogAndExitCode()
	{
		QTemporaryDir files;
		QVERIFY(files.isValid());
		QProcess child;
		child.start(QCoreApplication::applicationFilePath(), {"--plugin-error-child", files.filePath("missing.so"), "-platform", "offscreen"});
		QVERIFY(child.waitForStarted(5000));
		QVERIFY(child.waitForFinished(5000));
		QCOMPARE(child.exitStatus(), QProcess::NormalExit);
		QCOMPARE(child.exitCode(), 1);
		QVERIFY(child.readAllStandardOutput().contains("DIALOG"));
	}
};

int pluginErrorChild(QString const &path)
{
	PluginLoadError error;
	if (loadPlugin<TestPluginInstance, TestPluginInterface>("missing", &error, path)) return 2;
	bool correct = false;
	QTimer::singleShot(0, [&] {
		auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
		if (!box) return;
		correct = box->icon() == QMessageBox::Critical && box->textFormat() == Qt::PlainText
			&& box->text().contains(error.name) && box->text().contains(path) && box->text().contains(error.reason);
		box->accept();
	});
	showPluginLoadError(error);
	if (correct) std::cout << "DIALOG" << std::endl;
	return correct ? 1 : 2;
}

int runPluginLoadingTests(int argc, char **argv)
{
	PluginLoadingTests tests;
	return QTest::qExec(&tests, argc, argv);
}

#include "plugin_loading_tests.moc"
