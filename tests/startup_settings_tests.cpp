#include "ApplicationGlobal.h"
#include "SettingsDialog.h"

#include <QCheckBox>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

class StartupSettingsTests : public QObject {
	Q_OBJECT
private:
	std::unique_ptr<QTemporaryDir> config_;
	QString previous_config_path_;
	ApplicationSettings previous_settings_;

private slots:
	void init()
	{
		previous_config_path_ = global->config_file_path;
		previous_settings_ = global->appsettings;
		config_ = std::make_unique<QTemporaryDir>();
		QVERIFY(config_->isValid());
		global->config_file_path = config_->filePath("TreeWalker.ini");
	}

	void cleanup()
	{
		global->config_file_path = previous_config_path_;
		global->appsettings = previous_settings_;
		config_.reset();
	}

	void missingFileLoadsDefaultsWithoutCreatingFile()
	{
		global->appsettings.remember_and_restore_window_position = false;
		global->appsettings.strongly_draw_file_suffix = false;

		SettingsDialog::loadSettings(&global->appsettings);

		QVERIFY(global->appsettings.remember_and_restore_window_position);
		QVERIFY(global->appsettings.strongly_draw_file_suffix);
		QVERIFY(!QFile::exists(global->config_file_path));
	}

	void missingKeysKeepDefaults_data()
	{
		QTest::addColumn<QString>("key");
		QTest::newRow("saved window position only") << QString("Global/SaveWindowPosition");
		QTest::newRow("saved suffix emphasis only") << QString("View/StronglyDrawFileSuffix");
	}

	void missingKeysKeepDefaults()
	{
		QFETCH(QString, key);
		{
			QSettings stored(global->config_file_path, QSettings::IniFormat);
			stored.setValue(key, false);
			stored.sync();
			QCOMPARE(stored.status(), QSettings::NoError);
		}

		SettingsDialog::loadSettings(&global->appsettings);

		QCOMPARE(global->appsettings.remember_and_restore_window_position, key != "Global/SaveWindowPosition");
		QCOMPARE(global->appsettings.strongly_draw_file_suffix, key != "View/StronglyDrawFileSuffix");
	}

	void acceptedSettingsSurviveRestartBeforeOpeningDialog()
	{
		{
			SettingsDialog dialog(nullptr);
			auto *position = dialog.findChild<QCheckBox *>("checkBox_save_window_pos");
			auto *suffix = dialog.findChild<QCheckBox *>("checkBox_strong_suffix");
			QVERIFY(position && suffix);
			position->setChecked(false);
			suffix->setChecked(false);
			dialog.accept();
			QCOMPARE(dialog.result(), int(QDialog::Accepted));
		}

		global->appsettings = ApplicationSettings::defaultSettings();
		SettingsDialog::loadSettings(&global->appsettings);
		QVERIFY(!global->appsettings.remember_and_restore_window_position);
		QVERIFY(!global->appsettings.strongly_draw_file_suffix);

		SettingsDialog reopened(nullptr);
		auto *position = reopened.findChild<QCheckBox *>("checkBox_save_window_pos");
		auto *suffix = reopened.findChild<QCheckBox *>("checkBox_strong_suffix");
		QVERIFY(position && suffix);
		QVERIFY(!position->isChecked());
		QVERIFY(!suffix->isChecked());
		QCOMPARE(reopened.settings().remember_and_restore_window_position, global->appsettings.remember_and_restore_window_position);
		QCOMPARE(reopened.settings().strongly_draw_file_suffix, global->appsettings.strongly_draw_file_suffix);
	}

	void cancelledDialogKeepsRuntimeAndStoredSettings()
	{
		ApplicationSettings saved;
		saved.strongly_draw_file_suffix = false;
		SettingsDialog::saveSettings(&saved);
		SettingsDialog::loadSettings(&global->appsettings);
		QFile before(global->config_file_path);
		QVERIFY(before.open(QIODevice::ReadOnly));
		const QByteArray original = before.readAll();
		before.close();
		{
			SettingsDialog dialog(nullptr);
			auto *position = dialog.findChild<QCheckBox *>("checkBox_save_window_pos");
			auto *suffix = dialog.findChild<QCheckBox *>("checkBox_strong_suffix");
			QVERIFY(position && suffix);
			position->setChecked(false);
			suffix->setChecked(true);
			dialog.reject();
			QCOMPARE(dialog.result(), int(QDialog::Rejected));
		}

		QVERIFY(global->appsettings.remember_and_restore_window_position);
		QVERIFY(!global->appsettings.strongly_draw_file_suffix);
		QFile after(global->config_file_path);
		QVERIFY(after.open(QIODevice::ReadOnly));
		QCOMPARE(after.readAll(), original);
		ApplicationSettings reloaded;
		SettingsDialog::loadSettings(&reloaded);
		QVERIFY(reloaded.remember_and_restore_window_position);
		QVERIFY(!reloaded.strongly_draw_file_suffix);
	}
};

int runStartupSettingsTests(int argc, char **argv)
{
	StartupSettingsTests tests;
	return QTest::qExec(&tests, argc, argv);
}

#include "startup_settings_tests.moc"
