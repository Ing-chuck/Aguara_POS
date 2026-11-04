include($$AGUARA_SRC_ROOT/aguara.pri)

TEMPLATE = lib
TARGET   = aguara_interfaces
QT      += core gui widgets svg
DEFINES += AGUARA_INTERFACES_LIBRARY

# The import library goes to lib/ (linking); the DLL goes next to aguara.exe.
DESTDIR    = $$AGUARA_LIB_DIR
DLLDESTDIR = $$AGUARA_BIN_DIR

SOURCES += aguarastyle.cpp \
           barcode.cpp \
           interfaces.cpp \
           money.cpp \
           quantity.cpp \
           salesservice.cpp
HEADERS += aguara_global.h \
           aguarastyle.h \
           aguarahost.h \
           barcode.h \
           cashservice.h \
           printservice.h \
           inventoryservice.h \
           quantity.h \
           interfaces.h \
           messages.h \
           money.h \
           productservice.h \
           salesservice.h \
           userservice.h \
           version.h

# The theme (style sheet, icons, logo), shared by the core and the modules.
RESOURCES += theme/theme.qrc
