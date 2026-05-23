#ifndef RENAMEDIALOG_H
#define RENAMEDIALOG_H

#include <QDialog>

namespace Ui {
class RenameDialog;
}

class RenameDialog : public QDialog {
	Q_OBJECT
private:
	Ui::RenameDialog *ui;
	QString path_;
	QString dir_;
public:
	enum Mode {
		File,
		Folder,
	};
	Mode mode_ = File;
public:
	explicit RenameDialog(Mode mode, const QString &path, QWidget *parent = nullptr);
	~RenameDialog();
	QString text() const;
};

#endif // RENAMEDIALOG_H
