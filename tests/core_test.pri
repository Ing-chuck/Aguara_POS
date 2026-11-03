# A test suite that links the core library (database, services, UI).
include($$PWD/test.pri)
include($$AGUARA_SRC_ROOT/src/core/core.pri)

QT += sql
