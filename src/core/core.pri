# Link the core static library: used by the application (src/app) and by the
# tests. A static library doesn't carry its own dependencies, so they are
# listed here once.

INCLUDEPATH += $$AGUARA_SRC_ROOT/src/core
DEPENDPATH  += $$AGUARA_SRC_ROOT/src/core
QT          += sql widgets network

LIBS += -L$$AGUARA_LIB_DIR -laguara_core -laguara_interfaces
win32: LIBS += -lcrypt32                # DPAPI: CryptProtectData (database.cpp)
win32-g++: PRE_TARGETDEPS += $$AGUARA_LIB_DIR/libaguara_core.a \
                             $$AGUARA_LIB_DIR/libaguara_interfaces.a

# copy_libpq(dir): copy the 32-bit PostgreSQL client DLLs into dir.
# Qt's qsqlpsql driver loads libpq.dll at runtime (only needed for the
# "server on the LAN" setup). Point AGUARA_LIBPQ_DIR (qmake argument or
# environment variable) at the bin folder of the 32-bit PostgreSQL 10.x
# Windows binaries.
defineTest(copy_libpq) {
    dir = $$1
    libpq_dir = $$AGUARA_LIBPQ_DIR
    isEmpty(libpq_dir): libpq_dir = $$(AGUARA_LIBPQ_DIR)
    isEmpty(libpq_dir): return(true)
    for (dll, $$list(libpq.dll libssl-1_1.dll libcrypto-1_1.dll libintl-8.dll libiconv-2.dll)) {
        !exists($$libpq_dir/$$dll): error("AGUARA_LIBPQ_DIR: $$dll not found in $$libpq_dir")
        QMAKE_POST_LINK += $$QMAKE_COPY $$shell_quote($$shell_path($$libpq_dir/$$dll)) \
                           $$shell_quote($$shell_path($$dir)) $$escape_expand(\\n\\t)
    }
    export(QMAKE_POST_LINK)
    return(true)
}
