include(common.pri)
include(TreeWalker.pri)
CONFIG(debug,debug|release):TARGET = TreeWalkerd
CONFIG(release,debug|release):TARGET = TreeWalker
DESTDIR = $$PWD/_bin
CPP_STD = c++17
gcc:QMAKE_CXXFLAGS += -std=$$CPP_STD -Wall -Wextra -Werror=return-type -Werror=trigraphs -Wno-switch -Wno-reorder -Wno-unused-parameter -Wno-unused-parameter
msvc:DEFINES += NOMINMAX

