QT += core gui widgets

TARGET = TreeWalker
TEMPLATE = app
CONFIG += $$CPP_STD

INCLUDEPATH += $$PWD/src
# INCLUDEPATH += $$PWD/UGlobalHotkey/lib
INCLUDEPATH += $$PWD/../QtDarkStyle

win32:LIBS += -lcomctl32 -luser32 -lole32 -lgdi32

SOURCES += \
	$$PWD/src/FileItemModel.cpp \
	$$PWD/src/FileTableView.cpp \
	$$PWD/src/FolderTreeView.cpp \
	$$PWD/src/ItemIdList.cpp \
	$$PWD/src/MyListWidget.cpp \
	$$PWD/src/ThumbnailLoader.cpp \
	$$PWD/src/ThumbnailView.cpp \
	$$PWD/src/darktheme/DarkStyle.cpp \
	$$PWD/src/darktheme/LightStyle.cpp \
	$$PWD/src/darktheme/MyCommonStyle.cpp \
	$$PWD/src/darktheme/NinePatch.cpp \
	$$PWD/src/darktheme/TraditionalWindowsStyleTreeControl.cpp \
	$$PWD/src/misc.cpp \
	$$PWD/src/q/DateTime.cpp \
	$$PWD/src/q/Dir.cpp \
	$$PWD/src/q/DirIterator.cpp \
	$$PWD/src/q/FileInfo.cpp \
	$$PWD/src/realpath.cpp \
	src/AbstractFileSystemProvider.cpp \
	src/AbstractSettingForm.cpp \
	src/ApplicationGlobal.cpp \
	src/ApplicationSettings.cpp \
	src/BasicFileSystemProvider.cpp \
	src/BookmarkToolButton.cpp \
	src/FetchLocationThread.cpp \
	src/MainWindow.cpp \
	src/MySettings.cpp \
	src/ReadOnlyLineEdit.cpp \
	src/ReadOnlyPlainTextEdit.cpp \
	src/RenameDialog.cpp \
	src/SettingExampleForm.cpp \
	src/SettingGeneralForm.cpp \
	src/SettingsDialog.cpp \
	src/StatusLabel.cpp \
	src/charvec.cpp \
	src/joinpath.cpp \
	src/Theme.cpp \
	src/main.cpp

HEADERS += \
	$$PWD/src/FileItemModel.h \
	$$PWD/src/FileTableView.h \
	$$PWD/src/FolderTreeView.h \
	$$PWD/src/ItemIdList.h \
	$$PWD/src/MyListWidget.h \
	$$PWD/src/ThumbnailLoader.h \
	$$PWD/src/ThumbnailView.h \
	$$PWD/src/darktheme/DarkStyle.h \
	$$PWD/src/darktheme/LightStyle.h \
	$$PWD/src/darktheme/MyCommonStyle.h \
	$$PWD/src/darktheme/NinePatch.h \
	$$PWD/src/darktheme/TraditionalWindowsStyleTreeControl.h \
	$$PWD/src/darktheme/darkstylehelper.i \
	$$PWD/src/misc.h \
	$$PWD/src/q/DateTime.h \
	$$PWD/src/q/Dir.h \
	$$PWD/src/q/DirIterator.h \
	$$PWD/src/q/FileInfo.h \
	$$PWD/src/q/helper.h \
	$$PWD/src/realpath.h \
	$$PWD/src/xdg.h \
	MyTreeWidget.h \
	src/AbstractFileSystemProvider.h \
	src/AbstractSettingForm.h \
	src/ApplicationGlobal.h \
	src/ApplicationSettings.h \
	src/BasicFileSystemProvider.h \
	src/BookmarkToolButton.h \
	src/FetchLocationThread.h \
	src/MainWindow.h \
	src/MySettings.h \
	src/ReadOnlyLineEdit.h \
	src/ReadOnlyPlainTextEdit.h \
	src/RenameDialog.h \
	src/SettingExampleForm.h \
	src/SettingGeneralForm.h \
	src/SettingsDialog.h \
	src/StatusLabel.h \
	src/charvec.h \
	src/joinpath.h \
	src/Theme.h \
	src/jstream.h

FORMS += \
	src/MainWindow.ui \
	src/RenameDialog.ui \
	src/SettingExampleForm.ui \
	src/SettingGeneralForm.ui \
	src/SettingsDialog.ui

win32:SOURCES += src/WindowsFileSystemProvider.cpp \
	$$PWD/src/NetworkDiscoveryThread.cpp \
	$$PWD/src/wstring.cpp \
	src/WindowsShellAPI.cpp
win32:HEADERS += src/WindowsFileSystemProvider.h \
	$$PWD/src/NetworkDiscoveryThread.h \
	$$PWD/src/wstring.h \
	src/WindowsShellAPI.h

!win32:SOURCES += \
	$$PWD/src/xdg.cpp

!win32:HEADERS += \
	$$PWD/src/xdg.h
