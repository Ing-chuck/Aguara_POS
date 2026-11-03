TARGET = modulo_template_MI
include(../module.pri)

SOURCES   += mymodule.cpp \
             mymodulefactory.cpp
HEADERS   += mymodule.h \
             mymodulefactory.h
FORMS     += mymodule.ui
RESOURCES += resources.qrc
DISTFILES += module.json
