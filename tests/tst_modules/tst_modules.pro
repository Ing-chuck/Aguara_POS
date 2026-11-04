TARGET = tst_modules
include(../core_test.pri)
QT += printsupport

# The label layout is internal to the inventory module: compile it in directly.
INVENTORY_DIR = $$AGUARA_SRC_ROOT/src/modules/inventory_manager
SALES_DIR = $$AGUARA_SRC_ROOT/src/modules/sales_module
INCLUDEPATH += $$INVENTORY_DIR $$SALES_DIR
HEADERS += $$INVENTORY_DIR/labelsheet.h \
           $$SALES_DIR/closingsheet.h
SOURCES += tst_modules.cpp \
           $$INVENTORY_DIR/labelsheet.cpp \
           $$SALES_DIR/closingsheet.cpp
