# The executable: aguara.exe. Everything but main() lives in the core library.
include($$AGUARA_SRC_ROOT/aguara.pri)
include($$AGUARA_SRC_ROOT/src/core/core.pri)

TEMPLATE = app
TARGET   = aguara
QT      += core gui

DESTDIR  = $$AGUARA_BIN_DIR

SOURCES += main.cpp

copy_libpq($$AGUARA_BIN_DIR)

# Windows: the icon of aguara.exe (taskbar, Explorer, shortcuts) and the name
# shown in Task Manager. aguara.ico holds 16-256 px renderings of the logo
# (src/interfaces/theme/icons/logo.svg).
RC_ICONS = aguara.ico
QMAKE_TARGET_PRODUCT     = AguaraPOS
QMAKE_TARGET_DESCRIPTION = AguaraPOS - punto de venta
QMAKE_TARGET_COMPANY     = AguaraPOS
