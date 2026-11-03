TARGET = modulo_sales_module
include(../module.pri)

SOURCES   += salesmodule.cpp \
             salesmodulefactory.cpp
HEADERS   += salesmodule.h \
             salesmodulefactory.h
FORMS     += salesmodule.ui
RESOURCES += resources.qrc
DISTFILES += module.json
