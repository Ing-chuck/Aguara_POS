QT       += sql core gui charts

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    module_dialog.cpp

HEADERS += \
    connection.h \
    interfaces.h \
    mainwindow.h \
    module_dialog.h \
    version.h

FORMS += \
    mainwindow.ui

TRANSLATIONS += \
    aguara_es_PY.ts

LIBS           = -L../modules/built-plugins

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
