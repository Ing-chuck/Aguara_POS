TEMPLATE      = lib
CONFIG       += plugin c++11
QT           += widgets sql
INCLUDEPATH  += ../../interfaces
LIBS         += -L../../build-static -l$$qtLibraryTarget(interfaces)
SOURCES      += inventorywidget.cpp \
                inventoryfactory.cpp
HEADERS      += inventorywidget.h \
                inventoryfactory.h
RESOURCES    += resources.qrc
FORMS         = inventorywidget.ui
TARGET        = $$qtLibraryTarget(modulo_control_inventario_)
DESTDIR       = ../build-modules
