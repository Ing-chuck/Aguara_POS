#ifndef TESTSUPPORT_H
#define TESTSUPPORT_H

// Helpers shared by the suites that need a database.
//
// Backends: SQLite always (a file in a temporary folder). PostgreSQL only when
// AGUARA_TEST_PG_HOST is set (plus _PORT, _USER, _PASSWORD), and only on a
// database named aguara_test, because the suites delete everything in it.

#include "connectionmonitor.h"
#include "corehost.h"
#include "database.h"
#include "sqlcashservice.h"
#include "sqlcustomerservice.h"
#include "sqlinventoryservice.h"
#include "sqlprintservice.h"
#include "sqlproductservice.h"
#include "sqlpromotionservice.h"
#include "sqlreportservice.h"
#include "sqlreturnservice.h"
#include "sqlsalesservice.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <functional>
#include <memory>

namespace TestSupport {

inline SalePayment inCash(Money tendered)
{
    SalePayment p;
    p.method = QString::fromLatin1(PaymentMethod::Cash);
    p.amount = tendered;
    return p;
}

inline SalePayment byVoucher(const char *method, Money amount, const QString &reference)
{
    SalePayment p;
    p.method = QString::fromLatin1(method);
    p.amount = amount;
    p.reference = reference;
    return p;
}

inline bool postgresConfigured()
{
    return !qEnvironmentVariableIsEmpty("AGUARA_TEST_PG_HOST");
}

inline DatabaseSettings postgresSettings()
{
    DatabaseSettings s;
    s.backend  = DatabaseSettings::PostgreSQL;
    s.host     = qEnvironmentVariable("AGUARA_TEST_PG_HOST");
    s.port     = qEnvironmentVariableIsSet("AGUARA_TEST_PG_PORT") ? qEnvironmentVariableIntValue("AGUARA_TEST_PG_PORT") : 5432;
    s.name     = QStringLiteral("aguara_test");
    s.user     = qEnvironmentVariable("AGUARA_TEST_PG_USER", QStringLiteral("aguara"));
    s.password = qEnvironmentVariable("AGUARA_TEST_PG_PASSWORD");
    return s;
}

inline const char *postgresSkipReason()
{
    return "PostgreSQL not configured: set AGUARA_TEST_PG_HOST (see tests/README.md)";
}

/// Global data rows: every test function runs once per backend.
/// Use in initTestCase_data(); read with QFETCH_GLOBAL(int, backend).
inline void addBackendRows()
{
    QTest::addColumn<int>("backend");
    QTest::newRow("sqlite") << int(DatabaseSettings::SQLite);
    QTest::newRow("postgresql") << int(DatabaseSettings::PostgreSQL);
}

/// Process events for up to `ms` until `done` returns true.
inline bool waitFor(const std::function<bool()> &done, int ms = 5000)
{
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(10);
    }
    return done();
}

/// A migrated, empty core database with the services on top, as main() sets
/// them up. Each open() starts from scratch.
class CoreFixture
{
public:
    explicit CoreFixture(const QString &connectionName = QStringLiteral("aguara"))
        : database(connectionName) {}
    ~CoreFixture() { close(); }

    /// SQLite: a new file in `dir`. PostgreSQL: empties aguara_test.
    /// Returns false with `error` set.
    bool open(DatabaseSettings::Backend backend, const QString &dir, QString *error)
    {
        close();
        QStandardPaths::setTestModeEnabled(true);   // never the real aguara.ini
        QFile::remove(DatabaseSettings::settingsPath());
        printed.clear();
        printerFails = false;
        DatabaseSettings s;
        if (backend == DatabaseSettings::PostgreSQL) {
            s = postgresSettings();
        } else {
            static int counter = 0;
            s.sqlitePath = QStringLiteral("%1/test-%2.db").arg(dir).arg(++counter);
        }
        settings = s;
        if (!database.open(s)) {
            *error = database.lastError();
            return false;
        }
        if (backend == DatabaseSettings::PostgreSQL) {
            // Everything the test user created. Not DROP SCHEMA public: up to
            // PostgreSQL 14 that schema belongs to the superuser, not to the
            // database owner.
            QSqlQuery q(QSqlDatabase::database(database.connectionName()));
            if (!q.exec(QStringLiteral("DROP OWNED BY CURRENT_USER CASCADE"))) {
                *error = QStringLiteral("cannot reset the test database: %1").arg(q.lastError().text());
                return false;
            }
        }
        if (!database.migrate(QStringLiteral("core"), coreMigrations())) {
            *error = database.lastError();
            return false;
        }
        monitor.reset(new ConnectionMonitor(database.connectionName(), backend == DatabaseSettings::PostgreSQL));
        monitor->setHealthCheckInterval(0);     // tests drive it explicitly
        products.reset(new SqlProductService(monitor.get()));
        inventory.reset(new SqlInventoryService(monitor.get(), products.get()));
        cash.reset(new SqlCashService(monitor.get(), QStringLiteral("PC-TEST")));
        sales.reset(new SqlSalesService(monitor.get(), products.get(), cash.get()));
        static int queues = 0;
        sales->outbox()->setPath(QStringLiteral("%1/cola-%2.db").arg(dir).arg(++queues));
        print.reset(new SqlPrintService(monitor.get(), sales.get(), cash.get()));
        print->setRawSender([this](const QString &, const QByteArray &data, QString *problem) {
            if (printerFails) {
                *problem = QStringLiteral("La impresora no responde.");
                return false;
            }
            printed << data;
            return true;
        });
        customers.reset(new SqlCustomerService(monitor.get()));
        returns.reset(new SqlReturnService(monitor.get(), sales.get(), cash.get()));
        reports.reset(new SqlReportService(monitor.get(), products.get()));
        promotions.reset(new SqlPromotionService(monitor.get()));
        host.reset(new CoreHost);
        host->registerService(ProductService_iid, products.get());
        host->registerService(InventoryService_iid, inventory.get());
        host->registerService(CashService_iid, cash.get());
        host->registerService(SalesService_iid, sales.get());
        host->registerService(PrintService_iid, print.get());
        host->registerService(CustomerService_iid, customers.get());
        host->registerService(ReturnService_iid, returns.get());
        host->registerService(ReportService_iid, reports.get());
        host->registerService(PromotionService_iid, promotions.get());
        return true;
    }

    void close()
    {
        host.reset();
        promotions.reset();
        reports.reset();
        returns.reset();
        customers.reset();
        print.reset();
        sales.reset();
        cash.reset();
        inventory.reset();
        products.reset();
        monitor.reset();
        database.close();
    }

    QSqlDatabase connection() const { return QSqlDatabase::database(database.connectionName(), false); }

    DatabaseSettings settings;
    Database database;
    std::unique_ptr<ConnectionMonitor> monitor;
    std::unique_ptr<SqlProductService> products;
    std::unique_ptr<SqlInventoryService> inventory;
    std::unique_ptr<SqlCashService> cash;
    std::unique_ptr<SqlSalesService> sales;
    std::unique_ptr<SqlPrintService> print;
    std::unique_ptr<SqlCustomerService> customers;
    std::unique_ptr<SqlReturnService> returns;
    std::unique_ptr<SqlReportService> reports;
    std::unique_ptr<SqlPromotionService> promotions;
    std::unique_ptr<CoreHost> host;
    QList<QByteArray> printed;      // what a thermal printer would have received
    bool printerFails = false;
};

} // namespace TestSupport

/// In a test function of a suite with addBackendRows(): open `fixture` on the
/// current row's backend (skipping PostgreSQL when it isn't configured).
#define AGUARA_OPEN_FIXTURE(fixture, dir)                                           \
    do {                                                                            \
        QFETCH_GLOBAL(int, backend);                                                \
        if (backend == DatabaseSettings::PostgreSQL && !TestSupport::postgresConfigured()) \
            QSKIP(TestSupport::postgresSkipReason());                               \
        QString openError_;                                                         \
        if (!(fixture).open(DatabaseSettings::Backend(backend), (dir), &openError_)) \
            QFAIL(qPrintable(openError_));                                          \
    } while (0)

#endif // TESTSUPPORT_H
