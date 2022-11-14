TEMPLATE      = lib
CONFIG       += plugin c++11
QT           += widgets
INCLUDEPATH  += ../../interfaces
LIBS         += -L../../build-static -l$$qtLibraryTarget(interfaces)
SOURCES      += salesmodule.cpp \
                salesmodulefactory.cpp
HEADERS      += salesmodule.h \
                salesmodulefactory.h
RESOURCES    += resources.qrc
FORMS        += salesmodule.ui
TARGET        = $$qtLibraryTarget(modulo_sales_module)
DESTDIR       = ../build-modules
