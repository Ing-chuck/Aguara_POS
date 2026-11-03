# Top-level project: open this file in Qt Creator to build everything.
TEMPLATE = subdirs

SUBDIRS += interfaces core modules

interfaces.subdir = src/interfaces

core.file         = src/core/aguara.pro
core.depends      = interfaces

modules.subdir    = src/modules
modules.depends   = interfaces

DISTFILES += \
    README.md \
    .qmake.conf \
    aguara.pri \
    src/modules/module.pri
