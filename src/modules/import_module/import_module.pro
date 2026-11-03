TARGET = modulo_importar
include(../module.pri)

# No QT += sql: modules reach data only through the core's services.
HEADERS    = import_module.h
SOURCES    = import_module.cpp
RESOURCES += resources.qrc
DISTFILES += import_module.json
