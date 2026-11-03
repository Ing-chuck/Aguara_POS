TARGET = modulo_control_inventario
include(../module.pri)

# No QT += sql: modules reach data only through the core's services.
SOURCES   += inventorywidget.cpp \
             additemdialog.cpp \
             inventoryfactory.cpp \
             modifytabledialog.cpp
HEADERS   += inventorywidget.h \
             additemdialog.h \
             inventoryfactory.h \
             modifytabledialog.h
FORMS     += inventorywidget.ui \
             additemdialog.ui \
             modifytabledialog.ui
RESOURCES += resources.qrc
DISTFILES += inventory_module.json
