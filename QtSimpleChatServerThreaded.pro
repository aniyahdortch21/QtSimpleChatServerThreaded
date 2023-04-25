QT       += core gui
QT += network xml
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
        servermain.cpp \
    chatserver.cpp \
    serverworker.cpp \
    serverwindow.cpp

HEADERS += \
    chatserver.h \
    chatserver.h \
    serverworker.h \
    serverwindow.h

CONFIG += debug_and_release

FORMS += \
    serverwindow.ui
