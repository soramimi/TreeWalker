#include "RenameDialog.h"
#include "ui_RenameDialog.h"

RenameDialog::RenameDialog(Mode mode, QString const &path, QWidget *parent)
	: QDialog(parent)
	, ui(new Ui::RenameDialog)
	, mode_(mode)
{
	ui->setupUi(this);

	if (mode_ == Folder) {
		setWindowTitle(tr("Rename Folder"));
	} else if (mode_ == File) {
		setWindowTitle(tr("Rename File"));
	}

	ui->lineEdit_suffix->setEnabled(false);
	ui->checkBox_protect_suffix->setChecked(true);
	ui->stackedWidget->setCurrentWidget(ui->page_protect_extension);

	connect(ui->checkBox_protect_suffix, &QCheckBox::toggled, [this](bool checked) {
		if (checked) {
			ui->stackedWidget->setCurrentWidget(ui->page_protect_extension);
			ui->lineEdit_basename->setFocus();
		} else {
			ui->stackedWidget->setCurrentWidget(ui->page_entire);
			ui->lineEdit_entirename->setFocus();
		}
	});

	connect(ui->lineEdit_entirename, &QLineEdit::textChanged, [this](const QString &text) {
		int i = text.lastIndexOf('.');
		bool b = ui->lineEdit_basename->blockSignals(true);
		if (i > 0) {
			QString basename = text.left(i);
			QString suffix = text.mid(i + 1);
			ui->lineEdit_basename->setText(basename);
			ui->lineEdit_suffix->setText(suffix);
		} else {
			ui->lineEdit_basename->setText(text);
			ui->lineEdit_suffix->clear();
		}
		ui->lineEdit_basename->blockSignals(b);
	});

	connect(ui->lineEdit_basename, &QLineEdit::textChanged, [this](const QString &text) {
		bool b = ui->lineEdit_entirename->blockSignals(true);
		QString name;
		QString suffix = ui->lineEdit_suffix->text();
		if (suffix.isEmpty()) {
			name = text;
		} else {
			name = text + '.' + suffix;
		}
		ui->lineEdit_entirename->setText(name);
		ui->lineEdit_entirename->blockSignals(b);
	});

	path_ = path;
	QString name = path_;
	int i = name.lastIndexOf('/');
	if (i < 0) {
		i = name.lastIndexOf('\\');
	}
	if (i < 0) {
		dir_ = {};
	} else {
		dir_ = name.left(i);
		name = name.mid(i + 1);
	}
	ui->lineEdit_entirename->setText(name);

	ui->lineEdit_basename->setFocus();
}

RenameDialog::~RenameDialog()
{
	delete ui;
}

QString RenameDialog::text() const
{
	return {};
}
