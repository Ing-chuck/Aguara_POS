TEMPLATE      = lib
CONFIG       += plugin c++11
QT           += sql
INCLUDEPATH  += ../../core
HEADERS       = import_module.h
SOURCES       = import_module.cpp
TARGET        = $$qtLibraryTarget(modulo_importar_)
DESTDIR       = ../build-modules
