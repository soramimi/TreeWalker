#include "ApplicationGlobal.h"

#ifdef Q_OS_WIN
#include "WindowsShellAPI.h"
#endif


ApplicationGlobal::ApplicationGlobal()
{
#ifdef Q_OS_WIN
	shapi = std::make_shared<WindowsShellAPI>();
#endif
}

void ApplicationGlobal::init(QApplication *a)
{
	(void)a;
	// filetype.open();
}

