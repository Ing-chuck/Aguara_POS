# Top-level project: open this file in Qt Creator to build everything.
TEMPLATE = subdirs

SUBDIRS += interfaces core app modules tests

interfaces.subdir = src/interfaces

core.subdir       = src/core                # static library: data, services, UI
core.depends      = interfaces

app.subdir        = src/app                 # aguara.exe
app.depends       = core

modules.subdir    = src/modules
modules.depends   = interfaces

tests.subdir      = tests                   # Qt Test suites: "make check" runs them
tests.depends     = core modules            # some suites load the module plugins

# Skip the tests with: qmake CONFIG+=no_tests
no_tests: SUBDIRS -= tests

DISTFILES += \
    README.md \
    .qmake.conf \
    aguara.pri \
    src/modules/module.pri \
    doc/roadmap.md \
    .github/workflows/ci.yml
