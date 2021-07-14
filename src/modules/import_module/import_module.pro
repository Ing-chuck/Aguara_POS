TEMPLATE      = lib
CONFIG       += plugin
QT           += widgets sql
INCLUDEPATH  += ../../core
HEADERS       = import_module.h
SOURCES       = import_module.cpp
TARGET        = $$qtLibraryTarget(modulo_importar_)
DESTDIR       = ../build-modules

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
