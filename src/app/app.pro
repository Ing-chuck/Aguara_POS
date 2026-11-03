# The executable: aguara.exe. Everything but main() lives in the core library.
include($$AGUARA_SRC_ROOT/aguara.pri)
include($$AGUARA_SRC_ROOT/src/core/core.pri)

TEMPLATE = app
TARGET   = aguara
QT      += core gui

DESTDIR  = $$AGUARA_BIN_DIR

SOURCES += main.cpp

copy_libpq($$AGUARA_BIN_DIR)
