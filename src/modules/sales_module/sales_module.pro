TARGET = modulo_sales_module
include(../module.pri)

QT += printsupport

SOURCES   += cashdialogs.cpp \
             closingsheet.cpp \
             saledialogs.cpp \
             salesmodule.cpp \
             salesmodulefactory.cpp
HEADERS   += cashdialogs.h \
             closingsheet.h \
             saledialogs.h \
             salesmodule.h \
             salesmodulefactory.h
DISTFILES += module.json
