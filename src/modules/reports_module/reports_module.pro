# modulo_stats: modules load in file-name order, and this keeps Ctrl+3 on Ventas
TARGET = modulo_stats
include(../module.pri)

QT += printsupport

SOURCES   += reportsfactory.cpp \
             reportspage.cpp
HEADERS   += reportsfactory.h \
             reportspage.h
DISTFILES += module.json
