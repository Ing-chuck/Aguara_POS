// Several tills on one PostgreSQL server: change notifications between them,
// and recovery when the server restarts or goes away.
//
// Needs a test server it may stop and start: AGUARA_TEST_PG_HOST (etc.) plus
// AGUARA_TEST_PG_CTL (pg_ctl.exe) and AGUARA_TEST_PG_DATA (its data folder).
// tests/run-postgresql-tests.sh sets all of that up. Without them: skipped.
//
// The functions run in order and share the two tills, like a shop does.
#include "testsupport.h"
#include "lowstockwatcher.h"

#include <QAbstractItemModel>
#include <QProcess>
#include <QSignalSpy>
#include <QtTest>

using TestSupport::CoreFixture;
using TestSupport::waitFor;

class tst_Reconnect : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void otherTillHearsChanges();
    void otherTillHearsLowStock();
    void readRetriesAfterServerRestart();
    void transactionAfterServerRestart();
    void idleTillRecoversAndListensAgain();
    void outageIsReportedAndFailsFast();
    void recoversWhenServerIsBack();
    void saleSurvivesARealOutage();

private:
    bool pgctl(const QStringList &args);
    bool restartServer() { return pgctl({ QStringLiteral("stop") }) && startServer(); }
    bool startServer();
    bool saveProduct(const QString &code, const QString &description, Money price);
    bool till2Shows(int rows) { return waitFor([&] { return m_model2->rowCount() == rows; }, 8000); }

    QTemporaryDir m_tmp;
    CoreFixture m_till1{ QStringLiteral("till1") };
    Database m_till2db{ QStringLiteral("till2") };
    std::unique_ptr<ConnectionMonitor> m_monitor2;
    std::unique_ptr<SqlProductService> m_products2;
    QAbstractItemModel *m_model2 = nullptr;
    int m_rows = 0;                 // what till 2 should show
    QString m_pgCtl;
    QString m_pgData;
};

void tst_Reconnect::initTestCase()
{
    if (!TestSupport::postgresConfigured())
        QSKIP(TestSupport::postgresSkipReason());
    m_pgCtl = qEnvironmentVariable("AGUARA_TEST_PG_CTL");
    m_pgData = qEnvironmentVariable("AGUARA_TEST_PG_DATA");
    if (m_pgCtl.isEmpty() || m_pgData.isEmpty())
        QSKIP("needs AGUARA_TEST_PG_CTL and AGUARA_TEST_PG_DATA to restart the server");

    QString error;
    QVERIFY2(m_till1.open(DatabaseSettings::PostgreSQL, m_tmp.path(), &error), qPrintable(error));
    QVERIFY2(m_till2db.open(m_till1.settings), qPrintable(m_till2db.lastError()));
    QVERIFY(m_till2db.migrate(QStringLiteral("core"), coreMigrations()));     // already there: no-op
    m_monitor2.reset(new ConnectionMonitor(m_till2db.connectionName(), true));
    m_monitor2->setHealthCheckInterval(0);
    m_products2.reset(new SqlProductService(m_monitor2.get()));
    m_model2 = m_products2->createProductTableModel(m_products2.get());
}

void tst_Reconnect::cleanupTestCase()
{
    if (!m_pgCtl.isEmpty())
        startServer();              // leave it running even if a test failed mid-outage
    m_products2.reset();
    m_monitor2.reset();
    m_till2db.close();
    m_till1.close();
}

bool tst_Reconnect::pgctl(const QStringList &args)
{
    QProcess p;
    // the server must not inherit our console handles (it outlives the call)
    p.setStandardOutputFile(QProcess::nullDevice());
    p.setStandardErrorFile(QProcess::nullDevice());
    p.start(m_pgCtl, QStringList{ QStringLiteral("-D"), m_pgData, QStringLiteral("-w"),
                                  QStringLiteral("-m"), QStringLiteral("fast") } + args);
    return p.waitForFinished(60000) && p.exitCode() == 0;
}

bool tst_Reconnect::startServer()
{
    return pgctl({ QStringLiteral("-o"), QStringLiteral("-p %1 -c listen_addresses=localhost").arg(m_till1.settings.port),
                   QStringLiteral("-l"), m_pgData + QStringLiteral("/../pg.log"), QStringLiteral("start") })
        || pgctl({ QStringLiteral("status") });     // already running
}

bool tst_Reconnect::saveProduct(const QString &code, const QString &description, Money price)
{
    Product p;
    p.code = code;
    p.description = description;
    p.price = price;
    const bool ok = m_till1.products->saveProduct(p);
    if (!ok)
        qWarning() << "save failed:" << m_till1.products->lastError();
    return ok;
}

void tst_Reconnect::otherTillHearsChanges()
{
    m_rows = m_model2->rowCount();
    QVERIFY(saveProduct(QStringLiteral("5005"), QStringLiteral("GALLETITAS"), 4500));   // till 1 writes and NOTIFYs
    QVERIFY(till2Shows(++m_rows));                                                       // till 2 refreshes by itself
    QCOMPARE(m_products2->product(QStringLiteral("5005"))->price, Money(4500));
}

/// A sale or an adjustment at one till reaches who's signed in at the other.
void tst_Reconnect::otherTillHearsLowStock()
{
    Product p;
    p.code = QStringLiteral("5006");
    p.description = QStringLiteral("YERBA");
    p.price = 10000;
    p.stock = AguaraQuantity::whole(10);
    p.stockMin = AguaraQuantity::whole(5);
    QVERIFY(m_till1.products->saveProduct(p));
    QVERIFY(till2Shows(++m_rows));

    SqlInventoryService inventory2(m_monitor2.get(), m_products2.get());
    CoreHost host2;
    QStringList titles;
    connect(&host2, &CoreHost::notificationRequested, this,
            [&titles](const ModuleMsg &msg) { titles << msg.title; });
    LowStockWatcher watcher(m_products2.get(), &inventory2, nullptr, &host2);
    watcher.check();
    QCOMPARE(titles, QStringList());

    QVERIFY(m_till1.inventory->adjustStock(p.code, -AguaraQuantity::whole(6), QStringLiteral("rotura")));
    QVERIFY(waitFor([&titles] { return !titles.isEmpty(); }, 8000));
    QCOMPARE(titles, QStringList { QStringLiteral("1 artículo con stock bajo") });
    QVERIFY(m_till1.inventory->adjustStock(p.code, AguaraQuantity::whole(6), QStringLiteral("hallado")));
}

void tst_Reconnect::readRetriesAfterServerRestart()
{
    QVERIFY(restartServer());               // both tills' connections are now dead
    QSignalSpy reconnected(m_till1.monitor.get(), &ConnectionMonitor::reconnected);
    const auto p = m_till1.products->product(QStringLiteral("5005"));     // fails, reconnects, retries
    QVERIFY2(p.has_value(), qPrintable(m_till1.products->lastError()));
    QCOMPARE(p->description, QStringLiteral("GALLETITAS"));
    QCOMPARE(reconnected.count(), 1);
    QVERIFY(m_till1.monitor->isConnected());
}

void tst_Reconnect::transactionAfterServerRestart()
{
    QVERIFY(restartServer());
    QVERIFY(saveProduct(QStringLiteral("6006"), QStringLiteral("FIDEOS"), 7000));         // BEGIN finds the drop
    ++m_rows;
}

void tst_Reconnect::idleTillRecoversAndListensAgain()
{
    // till 2 did nothing through two restarts: the background check finds out
    m_monitor2->setHealthCheckInterval(200);
    QVERIFY(waitFor([&] { return m_model2->rowCount() == m_rows; }, 15000));             // refreshed on reconnect
    QVERIFY(saveProduct(QStringLiteral("7007"), QStringLiteral("ARROZ"), 6500));
    QVERIFY(till2Shows(++m_rows));                                                         // LISTEN again
}

void tst_Reconnect::outageIsReportedAndFailsFast()
{
    m_till1.monitor->setHealthCheckInterval(200);
    QSignalSpy lost(m_till1.monitor.get(), &ConnectionMonitor::connectionLost);
    QVERIFY(pgctl({ QStringLiteral("stop") }));
    // a refused connection to localhost takes seconds on Windows: wait long enough
    QVERIFY(waitFor([&] { return lost.count() > 0; }, 30000));
    QCOMPARE(lost.count(), 1);
    QVERIFY(!m_till1.monitor->isConnected());

    QElapsedTimer timer;
    timer.start();
    QVERIFY(!m_till1.products->product(QStringLiteral("5005")).has_value());
    QVERIFY2(timer.elapsed() < 1000, "a known outage must not wait on the network");
    const QString message = m_till1.products->lastError();
    QVERIFY2(message.startsWith(QString::fromUtf8("Sin conexión")), qPrintable(message));
    QVERIFY2(!message.contains(QStringLiteral("QPSQL")), qPrintable(message));

    // the background retries don't freeze the window
    qint64 worst = 0;
    timer.restart();
    for (qint64 last = 0; timer.elapsed() < 1500;) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        worst = qMax(worst, timer.elapsed() - last);
        last = timer.elapsed();
    }
    QVERIFY2(worst < 500, qPrintable(QStringLiteral("event loop stalled %1 ms").arg(worst)));
}

void tst_Reconnect::recoversWhenServerIsBack()
{
    QSignalSpy reconnected(m_till1.monitor.get(), &ConnectionMonitor::reconnected);
    QVERIFY(startServer());
    QVERIFY(waitFor([&] { return m_till1.monitor->isConnected(); }, 30000));
    QVERIFY(reconnected.count() >= 1);
    QVERIFY(m_till1.products->product(QStringLiteral("5005")).has_value());

    QVERIFY(waitFor([&] { return m_monitor2->isConnected(); }, 30000));
    QVERIFY(saveProduct(QStringLiteral("8008"), QStringLiteral("AZUCAR"), 8000));
    QVERIFY(till2Shows(++m_rows));                                                         // still listening
    m_till1.monitor->setHealthCheckInterval(0);
    m_monitor2->setHealthCheckInterval(0);
}

/// The server really stops, with the sale already on the screen and without
/// the monitor knowing: the statement of the sale is what finds out.
void tst_Reconnect::saleSurvivesARealOutage()
{
    DenominationCount note;
    note.denomination = 100000;
    note.count = 1;
    QVERIFY2(m_till1.cash->openSession({ note }), qPrintable(m_till1.cash->lastError()));
    Product p;
    p.code = QStringLiteral("9009");
    p.description = QStringLiteral("SAL");
    p.price = 5000;
    p.stock = AguaraQuantity::whole(10);
    QVERIFY(m_till1.products->saveProduct(p));
    ++m_rows;
    SaleLine line;
    line.code = p.code;
    line.description = p.description;
    line.unit = QStringLiteral("un");
    line.unitPrice = p.price;
    SaleDraft draft;
    draft.lines << line;
    QVERIFY(m_till1.sales->checkout(draft, { TestSupport::inCash(5000) }) > 0);

    QVERIFY(pgctl({ QStringLiteral("stop") }));
    const qint64 id = m_till1.sales->checkout(draft, { TestSupport::inCash(5000) });
    QVERIFY2(id < 0, qPrintable(QStringLiteral("%1: %2").arg(id).arg(m_till1.sales->lastError())));
    QVERIFY(!m_till1.sales->online());
    QCOMPARE(m_till1.sales->sale(id)->number, qint64(2));
    QVERIFY(m_till1.cash->currentSession().has_value());
    QCOMPARE(m_till1.sales->pendingSales().size(), 1);

    m_till1.monitor->setHealthCheckInterval(200);
    QVERIFY(startServer());
    QVERIFY(waitFor([&] { return m_till1.monitor->isConnected(); }, 30000));
    QVERIFY(waitFor([&] { return m_till1.sales->pendingSales().isEmpty(); }, 10000));
    m_till1.monitor->setHealthCheckInterval(0);

    QCOMPARE(m_till1.sales->sessionSales(m_till1.cash->currentSession()->id).size(), 2);
    QCOMPARE(m_till1.products->product(p.code)->stock, AguaraQuantity::whole(8));
    QCOMPARE(m_till1.sales->sessionSales(m_till1.cash->currentSession()->id).first().number, qint64(2));
}

QTEST_GUILESS_MAIN(tst_Reconnect)
#include "tst_reconnect.moc"
