QT += core testlib
CONFIG += console testcase c++17
TEMPLATE = app

SOURCES += test_planksharedscreen.cpp

HEADERS += ../../app/backend/planksharedscreen.h
INCLUDEPATH += ../../app/backend
