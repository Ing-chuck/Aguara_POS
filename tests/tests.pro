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
    tst_inventory \
    tst_sales \
    tst_print \
    tst_customers \
    tst_returns \
    tst_reports \
    tst_modules \
    tst_reconnect \
    tst_mainwindow \
    fixtures/old_api \
    fixtures/wont_load

DISTFILES += \
    README.md \
    test.pri \
    core_test.pri \
    common/testsupport.h \
    run-tests.sh \
    run-postgresql-tests.sh \
    test_autoversion.py
