QT           += sql core gui charts widgets
CONFIG       += c++11

SOURCES      +=    \
    interfaces.cpp \
    main.cpp       \
    mainwindow.cpp \
    module_dialog.cpp

HEADERS      +=     \
    connection.h    \
    interfaces.h    \
    mainwindow.h    \
    module_dialog.h \
    version.h

FORMS        += \
    mainwindow.ui

TRANSLATIONS += \
    aguara_es_PY.ts

LIBS          = -L../modules/built-plugins

RESOURCES += \
    resources.qrc
