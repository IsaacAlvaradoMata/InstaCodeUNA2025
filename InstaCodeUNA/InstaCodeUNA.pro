QT += core gui widgets

CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = InstaCodeUNA

SOURCES += main.cpp \
           window.cpp \
           parser.cpp

HEADERS += window.h \
           parser.h

INCLUDEPATH += /usr/local/Cellar/qt/6.9.1/include
LIBS += -L/usr/local/Cellar/qt/6.9.1/lib
