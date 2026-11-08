include(common.pri)
include(TreeWalker.pri)

QT += testlib
CONFIG += testcase console
CONFIG -= app_bundle
TARGET = TreeWalkerP1Tests
DESTDIR = $$PWD/_bin

SOURCES -= src/main.cpp $$PWD/src/main.cpp
SOURCES += $$PWD/tests/p1_regression.cpp
SOURCES += $$PWD/tests/model_notifications_tests.cpp
SOURCES += $$PWD/tests/address_input_tests.cpp
SOURCES += $$PWD/tests/special_locations_tests.cpp
win32:SOURCES += $$PWD/tests/network_discovery_tests.cpp
win32:SOURCES += $$PWD/tests/com_workers_tests.cpp
win32:LIBS += -lshell32
