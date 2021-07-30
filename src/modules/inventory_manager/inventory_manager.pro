TEMPLATE      = lib
CONFIG       += plugin c++11
QT           += widgets sql
INCLUDEPATH  += ../../core
QMAKE_LIBDIR += ../../build-aguara-Desktop_Qt_6_1_1_MSVC2019_64bit-Debug/debug/
LIBS         += interfaces.obj
SOURCES      += inventorywidget.cpp \
                inventoryfactory.cpp
HEADERS      += inventorywidget.h \
                inventoryfactory.h
RESOURCES    += resources.qrc
FORMS         = inventorywidget.ui
TARGET        = $$qtLibraryTarget(modulo_control_inventario_)
DESTDIR       = ../build-modules
