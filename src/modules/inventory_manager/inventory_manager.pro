TEMPLATE      = lib
CONFIG       += plugin c++11
QT           += widgets sql
INCLUDEPATH  += ../../core
SOURCES       = inventorywidget.cpp
HEADERS       = inventorywidget.h
RESOURCES    += resources.qrc
FORMS         = inventorywidget.ui
TARGET        = $$qtLibraryTarget(modulo_control_inventario_)
DESTDIR       = ../build-modules

