QT += core gui widgets network

TARGET = Launcher
TEMPLATE = app

SOURCES += main.cpp \
    logindialog.cpp \
    role_select_dialog.cpp

HEADERS += logindialog.h \
    role_select_dialog.h

FORMS += logindialog.ui \
    role_select_dialog.ui

# netapi
include(netapi/netapi.pri)

INCLUDEPATH += ../common netapi/mediator netapi/net
