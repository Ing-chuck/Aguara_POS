# Automated tests (Qt Test). One executable per suite, all in <build>/tests/bin.
#
#   make check                      run every suite (from the build folder)
#   Qt Creator: Tools > Tests > Run All Tests, or the Test Results pane
#
# PostgreSQL rows/suites run only when a test server is configured, see
# tests/README.md; otherwise they are skipped, not failed.
TEMPLATE = subdirs

SUBDIRS += \
    tst_money \
    tst_csvreader \
    tst_logging \
    tst_settings \
    tst_backup \
    tst_database \
    tst_productservice \
    tst_users \
    tst_modules \
    tst_reconnect \
    tst_mainwindow

DISTFILES += \
    README.md \
    test.pri \
    core_test.pri \
    common/testsupport.h \
    run-tests.sh \
    run-postgresql-tests.sh
