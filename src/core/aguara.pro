include($$AGUARA_SRC_ROOT/aguara.pri)

TEMPLATE = app
TARGET   = aguara
QT      += sql core gui charts widgets

DESTDIR  = $$AGUARA_BIN_DIR

LIBS           += -L$$AGUARA_LIB_DIR -laguara_interfaces
win32-g++: PRE_TARGETDEPS += $$AGUARA_LIB_DIR/libaguara_interfaces.a

SOURCES      +=     \
    connectionmonitor.cpp \
    corehost.cpp    \
    database.cpp    \
    databasesettingsdialog.cpp \
    main.cpp        \
    mainwindow.cpp  \
    module_dialog.cpp \
    notificationdialog.cpp \
    pagecero.cpp    \
    sqlproductservice.cpp

HEADERS      +=     \
    connectionmonitor.h \
    corehost.h      \
    database.h      \
    databasesettingsdialog.h \
    loadedmodule.h  \
    mainwindow.h    \
    module_dialog.h \
    notificationdialog.h \
    pagecero.h      \
    sqlproductservice.h

FORMS        +=     \
    mainwindow.ui   \
    notificationdialog.ui \
    pagecero.ui

TRANSLATIONS +=     \
    aguara_es_PY.ts

RESOURCES += \
    resources.qrc

# PostgreSQL client (only needed for the "server on the LAN" setup).
# Qt's qsqlpsql driver loads libpq.dll at runtime. Point AGUARA_LIBPQ_DIR
# (qmake argument or environment variable) at the bin folder of the 32-bit
# PostgreSQL 10.x Windows binaries and the DLLs are copied next to aguara.exe.
isEmpty(AGUARA_LIBPQ_DIR): AGUARA_LIBPQ_DIR = $$(AGUARA_LIBPQ_DIR)
!isEmpty(AGUARA_LIBPQ_DIR) {
    for (dll, $$list(libpq.dll libssl-1_1.dll libcrypto-1_1.dll libintl-8.dll libiconv-2.dll)) {
        !exists($$AGUARA_LIBPQ_DIR/$$dll): error("AGUARA_LIBPQ_DIR: $$dll not found in $$AGUARA_LIBPQ_DIR")
        QMAKE_POST_LINK += $$QMAKE_COPY $$shell_quote($$shell_path($$AGUARA_LIBPQ_DIR/$$dll)) \
                           $$shell_quote($$shell_path($$AGUARA_BIN_DIR)) $$escape_expand(\\n\\t)
    }
}
