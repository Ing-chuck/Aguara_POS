# Shared by every test suite: a console Qt Test executable in <build>/tests/bin
# that links the interfaces library. A suite's .pro sets TARGET and SOURCES
# and includes this file (or core_test.pri, for suites that need the core).

include($$AGUARA_SRC_ROOT/aguara.pri)

TEMPLATE = app
QT      += testlib widgets
CONFIG  += testcase console
CONFIG  -= app_bundle

DESTDIR  = $$AGUARA_BUILD_ROOT/tests/bin

INCLUDEPATH += $$PWD/common
LIBS        += -L$$AGUARA_LIB_DIR -laguara_interfaces
win32-g++: PRE_TARGETDEPS += $$AGUARA_LIB_DIR/libaguara_interfaces.a

# Where the module plugins are built: the module suites load the real DLLs.
DEFINES += AGUARA_TEST_MODULES_DIR=\\\"$$AGUARA_MODULES_DIR\\\"

# "make check" runs the test with <build>/bin first in PATH, so it finds
# aguara_interfaces.dll (and libpq.dll, when the build copied it there).
# Qt Creator adds the -L folders of LIBS instead; lib/ has the DLL too.
aguara_bin_path.name   = PATH
aguara_bin_path.value  = $$shell_path($$AGUARA_BIN_DIR)
aguara_bin_path.CONFIG = always_prepend
QT_TOOL_ENV += aguara_bin_path
