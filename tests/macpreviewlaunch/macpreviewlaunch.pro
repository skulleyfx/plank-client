QT += core testlib
CONFIG += console testcase c++17
TEMPLATE = app

SOURCES += test_macpreviewlaunch.cpp \
           ../../app/backend/outputtopology.cpp

HEADERS += ../../app/backend/macpreviewlaunch.h \
           ../../app/backend/outputtopology.h
INCLUDEPATH += ../../app/backend ../../moonlight-common-c/moonlight-common-c/src
