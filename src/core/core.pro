# The core: data, services, main window. Built as a static library so the
# application (src/app) and the tests (tests/) link the same code.
include($$AGUARA_SRC_ROOT/aguara.pri)

TEMPLATE = lib
CONFIG  += staticlib
TARGET   = aguara_core
QT      += sql widgets network printsupport    # network: QPasswordDigestor

DESTDIR  = $$AGUARA_LIB_DIR

SOURCES      +=     \
    backup.cpp      \
    connectionmonitor.cpp \
    corehost.cpp    \
    database.cpp    \
    databasesettingsdialog.cpp \
    escpos.cpp \
    printersettingsdialog.cpp \
    saleoutbox.cpp \
    sqlcustomerservice.cpp \
    sqlprintservice.cpp \
    reportpainter.cpp \
    activitylog.cpp \
    crashhandler.cpp \
    telemetry.cpp \
    telemetrydialog.cpp \
    sqlreportservice.cpp \
    sqlreturnservice.cpp \
    errors.cpp      \
    inactivitywatcher.cpp \
    logging.cpp     \
    mainwindow.cpp  \
    module_dialog.cpp \
    notificationdialog.cpp \
    pagecero.cpp    \
    passwordhash.cpp \
    sessiondialogs.cpp \
    sqlcashservice.cpp \
    sqlproductservice.cpp \
    sqlsalesservice.cpp \
    sqlinventoryservice.cpp \
    sqluserservice.cpp \
    lowstockwatcher.cpp \
    stockledger.cpp

HEADERS      +=     \
    backup.h        \
    connectionmonitor.h \
    corehost.h      \
    database.h      \
    databasesettingsdialog.h \
    escpos.h \
    printersettingsdialog.h \
    saleoutbox.h \
    sqlcustomerservice.h \
    sqlprintservice.h \
    reportpainter.h \
    activitylog.h \
    crashhandler.h \
    telemetry.h \
    telemetrydialog.h \
    sqlreportservice.h \
    sqlreturnservice.h \
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
    sqlcashservice.h \
    sqlproductservice.h \
    sqlsalesservice.h \
    sqlinventoryservice.h \
    sqluserservice.h \
    lowstockwatcher.h \
    stockledger.h

FORMS        +=     \
    mainwindow.ui   \
    notificationdialog.ui \
    pagecero.ui

TRANSLATIONS +=     \
    aguara_es_PY.ts

DISTFILES    +=     \
    core.pri
