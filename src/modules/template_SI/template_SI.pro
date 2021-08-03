TEMPLATE      = lib
CONFIG       += plugin c++11
QT           += widgets
INCLUDEPATH  += ../../interfaces
LIBS         += -L../../build-static -l$$qtLibraryTarget(interfaces)
SOURCES      += mymodule.cpp \
                mymodulefactory.cpp
HEADERS      += mymodule.h \
                mymodulefactory.h
RESOURCES    += resources.qrc
FORMS        += mymodule.ui
TARGET        = $$qtLibraryTarget(modulo_template_SI)
DESTDIR       = ../build-modules
