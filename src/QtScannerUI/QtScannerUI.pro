QT       += core gui widgets

TARGET = QtScannerUI
TEMPLATE = app

SOURCES += \
    main.cpp \
    mainwindow.cpp

HEADERS += \
    mainwindow.h

FORMS += \
    mainwindow.ui

LIBS += -L$$PWD/../../bin/Release -lFileScanner
INCLUDEPATH += $$PWD/../../include
