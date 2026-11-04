TARGET = modulo_control_inventario
QT += printsupport
include(../module.pri)

# No QT += sql: modules reach data only through the core's services.
SOURCES   += inventorywidget.cpp \
             inventorypages.cpp \
             labelsheet.cpp \
             stockdialogs.cpp \
             additemdialog.cpp \
             inventoryfactory.cpp \
             modifytabledialog.cpp
HEADERS   += inventorywidget.h \
             inventorypages.h \
             labelsheet.h \
             stockdialogs.h \
             additemdialog.h \
             inventoryfactory.h \
             modifytabledialog.h
FORMS     += inventorywidget.ui \
             additemdialog.ui \
             modifytabledialog.ui
DISTFILES += inventory_module.json
