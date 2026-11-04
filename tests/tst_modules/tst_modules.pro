TARGET = tst_modules
include(../core_test.pri)
QT += printsupport

# The label layout is internal to the inventory module: compile it in directly.
INVENTORY_DIR = $$AGUARA_SRC_ROOT/src/modules/inventory_manager
INCLUDEPATH += $$INVENTORY_DIR
HEADERS += $$INVENTORY_DIR/labelsheet.h
SOURCES += tst_modules.cpp \
           $$INVENTORY_DIR/labelsheet.cpp
