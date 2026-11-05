TARGET = tst_mainwindow
include(../core_test.pri)

# Modules that must not start: see tests/fixtures
DEFINES += AGUARA_TEST_FIXTURE_MODULES=\\\"$$AGUARA_BUILD_ROOT/tests/fixture_modules\\\"

SOURCES += tst_mainwindow.cpp
