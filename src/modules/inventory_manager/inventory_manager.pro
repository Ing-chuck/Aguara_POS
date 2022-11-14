TEMPLATE      = lib
CONFIG       += plugin c++11
QT           += widgets sql
INCLUDEPATH  += ../../interfaces
LIBS         += -L../../build-static -l$$qtLibraryTarget(interfaces)
SOURCES      += inventorywidget.cpp \
                additemdialog.cpp \
                inventoryfactory.cpp \
                inventorysqlmodel.cpp \
                modifytabledialog.cpp
HEADERS      += inventorywidget.h \
                additemdialog.h \
                inventoryfactory.h \
                inventorysqlmodel.h \
                modifytabledialog.h
RESOURCES    += resources.qrc
FORMS         = inventorywidget.ui \
    additemdialog.ui \
    modifytabledialog.ui
TARGET        = $$qtLibraryTarget(modulo_control_inventario_)
DESTDIR       = ../build-modules
