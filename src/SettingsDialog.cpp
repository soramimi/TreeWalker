#include "SettingsDialog.h"
#include "ui_SettingsDialog.h"
#include "MySettings.h"
// #include "common/misc.h"
// #include <QFileDialog>

static int page_number = 0;

SettingsDialog::SettingsDialog(MainWindow *parent)
	: QDialog(parent)
	, ui(new Ui::SettingsDialog)
{
	ui->setupUi(this);
	Qt::WindowFlags flags = windowFlags();
	flags &= ~Qt::WindowContextHelpButtonHint;
	setWindowFlags(flags);

	mainwindow_ = parent;

	auto AddPage = [&](AbstractSettingForm *page){
//		page->layout()->setMargin(0);
		auto *l = page->layout();
		if (l) {
			l->setContentsMargins(0, 0, 0, 0);
		}
		page->reset(mainwindow_, &settings_);
		QString name = page->windowTitle();
		QTreeWidgetItem *item = new QTreeWidgetItem();
		item->setText(0, name);
		item->setData(0, Qt::UserRole, QVariant::fromValue((uintptr_t)(QWidget *)page));
		ui->treeWidget->addTopLevelItem(item);
	};
	AddPage(ui->page_general);
//	AddPage(ui->page_example);

	loadSettings();

	ui->treeWidget->setCurrentItem(ui->treeWidget->topLevelItem(page_number));
}

SettingsDialog::~SettingsDialog()
{
	delete ui;
}

namespace {

template <typename T> class GetValue {
private:
public:
	MySettings &settings;
	QString name;
	GetValue(MySettings &s, QString const &name)
		: settings(s)
		, name(name)
	{
	}
//	void operator >> (T &value)
//	{
//		value = settings.value(name, value).template value<T>();
//	}
};

template <typename T> void operator >> (GetValue<T> const &l, T &r)
{
	r = l.settings.value(l.name, r).template value<T>();
}

template <> void operator >> (GetValue<QColor> const &l, QColor &r)
{
	QString s = l.settings.value(l.name, QString()).template value<QString>(); // 文字列で取得
	if (s.startsWith('#')) {
		r = s;
	}
}

template <typename T> class SetValue {
private:
public:
	MySettings &settings;
	QString name;
	SetValue(MySettings &s, QString const &name)
		: settings(s)
		, name(name)
	{
	}
//	void operator << (T const &value)
//	{
//		settings.setValue(name, value);
//	}
};

template <typename T> void operator << (SetValue<T> &&l, T const &r)
{
	l.settings.setValue(l.name, r);
}

template <> void operator << (SetValue<QColor> &&l, QColor const &r)
{
	QString s = QString::asprintf("#%02x%02x%02x", r.red(), r.green(), r.blue());
	l.settings.setValue(l.name, s);
}

} // namespace

void SettingsDialog::loadSettings(ApplicationSettings *as)
{
	MySettings s;

	*as = ApplicationSettings::defaultSettings();

	s.beginGroup("Global");
	GetValue<bool>(s, "SaveWindowPosition")                  >> as->remember_and_restore_window_position;
	s.endGroup();

	s.beginGroup("View");
	GetValue<bool>(s, "StronglyDrawFileSuffix")              >> as->strongly_draw_file_suffix;
	s.endGroup();

}

void SettingsDialog::saveSettings(ApplicationSettings const *as)
{
	MySettings s;

	s.beginGroup("Global");
	SetValue<bool>(s, "SaveWindowPosition")                  << as->remember_and_restore_window_position;
	s.endGroup();

	s.beginGroup("View");
	SetValue<bool>(s, "StronglyDrawFileSuffix")              << as->strongly_draw_file_suffix;
	s.endGroup();

}

void SettingsDialog::saveSettings()
{
	saveSettings(&settings_);
}

void SettingsDialog::exchange(bool save)
{
	QList<AbstractSettingForm *> forms = ui->stackedWidget->findChildren<AbstractSettingForm *>();
	for (AbstractSettingForm *form : forms) {
		form->exchange(save);
	}
}

void SettingsDialog::loadSettings()
{
	loadSettings(&settings_);
	exchange(false);
}

void SettingsDialog::done(int r)
{
	page_number = ui->treeWidget->currentIndex().row();
	QDialog::done(r);
}

void SettingsDialog::accept()
{
	exchange(true);
	saveSettings();
	done(QDialog::Accepted);
}

void SettingsDialog::on_treeWidget_currentItemChanged(QTreeWidgetItem *current, QTreeWidgetItem *previous)
{
	(void)previous;
	if (current) {
		uintptr_t p = current->data(0, Qt::UserRole).value<uintptr_t>();
		QWidget *w = reinterpret_cast<QWidget *>(p);
		Q_ASSERT(w);
		ui->stackedWidget->setCurrentWidget(w);
	}
}

