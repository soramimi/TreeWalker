include(common.pri)
include(TreeWalker.pri)
DESTDIR = $$PWD/_bin
DEFINES += NOMINMAX

RESOURCES += \
	src/resources/resources.qrc
