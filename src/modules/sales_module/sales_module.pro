TARGET = modulo_sales_module
include(../module.pri)

QT += printsupport

SOURCES   += cashdialogs.cpp \
             closingsheet.cpp \
             saledialogs.cpp \
             salesmodule.cpp \
             salesmodulefactory.cpp \
             tilldialogs.cpp
HEADERS   += cashdialogs.h \
             closingsheet.h \
             saledialogs.h \
             salesmodule.h \
             salesmodulefactory.h \
             tilldialogs.h
DISTFILES += module.json
