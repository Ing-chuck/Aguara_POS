QT           += sql core gui charts widgets
CONFIG       += c++11

SOURCES      +=     \
    main.cpp        \
    mainwindow.cpp  \
    module_dialog.cpp \
    notificationdialog.cpp

HEADERS      +=     \
    connection.h    \
    mainwindow.h    \
    module_dialog.h \
    notificationdialog.h

FORMS        +=     \
    mainwindow.ui   \
    notificationdialog.ui \
    pagecero.ui

TRANSLATIONS +=     \
    aguara_es_PY.ts

INCLUDEPATH  += ../interfaces
LIBS         += -L../modules/build-plugins
LIBS         += -L../build-static -l$$qtLibraryTarget(interfaces)

RESOURCES += \
    resources.qrc
