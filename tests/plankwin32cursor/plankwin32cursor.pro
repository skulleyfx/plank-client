QT += core testlib
CONFIG += console testcase c++17
TEMPLATE = app

SOURCES += test_plankwin32cursor.cpp

HEADERS += ../../app/streaming/plankwin32cursorlogic.h
INCLUDEPATH += ../../app/streaming
