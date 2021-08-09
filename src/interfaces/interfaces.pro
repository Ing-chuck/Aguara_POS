QT      += core widgets
TEMPLATE = lib
CONFIG  += staticlib c++11
SOURCES += interfaces.cpp
HEADERS += interfaces.h \
           messages.h \
           version.h
TARGET   = $$qtLibraryTarget(interfaces)
DESTDIR  = ../build-static
