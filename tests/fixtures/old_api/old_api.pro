TARGET = old_api
include($$AGUARA_SRC_ROOT/src/modules/module.pri)
DESTDIR = $$AGUARA_BUILD_ROOT/tests/fixture_modules

SOURCES += old_api.cpp
