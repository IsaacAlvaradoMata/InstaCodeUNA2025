QT += core gui widgets

CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = InstaCodeUNA

SOURCES += main.cpp \
           window.cpp \
           parser.cpp

HEADERS += window.h \
           parser.h
