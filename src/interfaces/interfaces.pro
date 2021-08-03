QT      += core widgets
TEMPLATE = lib
CONFIG  += staticlib c++11
SOURCES += interfaces.cpp
HEADERS += interfaces.h \
           version.h
TARGET   = $$qtLibraryTarget(interfaces)
DESTDIR  = ../build-static
