# The core: data, services, main window. Built as a static library so the
# application (src/app) and the tests (tests/) link the same code.
include($$AGUARA_SRC_ROOT/aguara.pri)

TEMPLATE = lib
CONFIG  += staticlib
TARGET   = aguara_core
QT      += sql widgets network    # network: QPasswordDigestor

DESTDIR  = $$AGUARA_LIB_DIR

SOURCES      +=     \
    backup.cpp      \
    connectionmonitor.cpp \
    corehost.cpp    \
    database.cpp    \
    databasesettingsdialog.cpp \
    errors.cpp      \
    inactivitywatcher.cpp \
    logging.cpp     \
    mainwindow.cpp  \
    module_dialog.cpp \
    notificationdialog.cpp \
    pagecero.cpp    \
    passwordhash.cpp \
    sessiondialogs.cpp \
    sqlproductservice.cpp \
    sqluserservice.cpp

HEADERS      +=     \
    backup.h        \
    connectionmonitor.h \
    corehost.h      \
    database.h      \
    databasesettingsdialog.h \
    errors.h        \
    inactivitywatcher.h \
    loadedmodule.h  \
    logging.h       \
    mainwindow.h    \
    module_dialog.h \
    notificationdialog.h \
    pagecero.h      \
    passwordhash.h  \
    sessiondialogs.h \
    sqlproductservice.h \
    sqluserservice.h

FORMS        +=     \
    mainwindow.ui   \
    notificationdialog.ui \
    pagecero.ui

TRANSLATIONS +=     \
    aguara_es_PY.ts

DISTFILES    +=     \
    core.pri
