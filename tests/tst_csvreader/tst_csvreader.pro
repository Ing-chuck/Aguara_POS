TARGET = tst_csvreader
include(../test.pri)

# The reader is internal to the import module: compile it in directly.
IMPORT_DIR = $$AGUARA_SRC_ROOT/src/modules/import_module
INCLUDEPATH += $$IMPORT_DIR
HEADERS += $$IMPORT_DIR/csvreader.h
SOURCES += tst_csvreader.cpp \
           $$IMPORT_DIR/csvreader.cpp
