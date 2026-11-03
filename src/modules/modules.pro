TEMPLATE = subdirs

SUBDIRS += \
    import_module \
    inventory_manager \
    sales_module

# The templates are starting points for new modules, not features. They are
# built in debug builds (so they keep compiling) but never in release builds,
# so they never appear in a shipped installation.
CONFIG(debug, debug|release) {
    SUBDIRS += \
        template_MI \
        template_SI
}
