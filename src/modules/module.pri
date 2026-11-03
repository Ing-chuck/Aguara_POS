# Boilerplate shared by every Aguara module (plugin).
# A module's .pro only needs to set TARGET, its sources, and include this file:
#
#     TARGET = my_module
#     include(../module.pri)

include($$AGUARA_SRC_ROOT/aguara.pri)

TEMPLATE = lib
CONFIG  += plugin
QT      += widgets

DESTDIR  = $$AGUARA_MODULES_DIR

LIBS           += -L$$AGUARA_LIB_DIR -laguara_interfaces
win32-g++: PRE_TARGETDEPS += $$AGUARA_LIB_DIR/libaguara_interfaces.a
