QT += core network testlib
CONFIG += console testcase c++17
TEMPLATE = app

SOURCES += test_plankcertificate.cpp \
           ../../app/backend/plankcertificate.cpp

HEADERS += ../../app/backend/plankcertificate.h
INCLUDEPATH += ../../app/backend
DEFINES += PLANK_CERTIFICATE_FIXTURES=\\\"$$PWD\\\"

unix: LIBS += -lcrypto
win32: LIBS += -llibcrypto
