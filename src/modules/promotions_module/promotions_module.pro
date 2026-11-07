# The name puts it last: modules load by file name and that order gives the
# Ctrl+1, Ctrl+2... shortcuts, so the others keep theirs.
TARGET = modulo_ventas_promociones
include(../module.pri)

SOURCES   += promotiondialog.cpp \
             promotionsfactory.cpp \
             promotionspage.cpp
HEADERS   += promotiondialog.h \
             promotionsfactory.h \
             promotionspage.h
DISTFILES += module.json
