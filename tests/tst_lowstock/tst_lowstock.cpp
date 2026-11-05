// The "stock bajo" notification follows the stock as it changes, on SQLite and
// PostgreSQL. What the screen does with it is in tst_mainwindow.
#include "testsupport.h"

#include "lowstockwatcher.h"

#include <QTemporaryDir>
#include <QtTest>

using TestSupport::CoreFixture;
using TestSupport::inCash;

namespace {

CashCount worth(Money amount)
{
    DenominationCount coin;
    coin.denomination = 50;
    coin.count = int(amount / 50);
    return { coin };
}

} // namespace

class tst_LowStock : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase_data() { TestSupport::addBackendRows(); }
    void cleanup();

    void nothingLowSaysNothing();
    void aSaleThatReachesTheMinimumNotifies();
    void onlyAChangeOfTheListSpeaks();
    void theNewArticleGoesFirst();
    void restockingUpdatesQuietly();
    void restockingEverythingTakesItBack();
    void aPurchaseCounts();
    void aStockCountCounts();
    void aReturnCounts();
    void voidingCounts();
    void editingTheMinimumCounts();
    void anArticleOutOfUseLeavesTheList();
    void anOutageIsNotARestock();
    void aSaleQueuedInAnOutageCountsWhenItArrives();

private:
    void start();
    bool article(const QString &code, Quantity stock, Quantity minimum = AguaraQuantity::whole(5));
    qint64 sell(const QString &code, Quantity quantity);
    void settle() { QTest::qWait(10); }         // a change is looked at right after it
    /// What was said since the last call.
    QStringList said();
    Quantity units(int count) const { return AguaraQuantity::whole(count); }

    QTemporaryDir m_tmp;
    CoreFixture m_core;
    std::unique_ptr<LowStockWatcher> m_watcher;
    QStringList m_said;
};

void tst_LowStock::cleanup()
{
    m_watcher.reset();
    m_core.close();
    m_said.clear();
}

void tst_LowStock::start()
{
    CoreHost *host = m_core.host.get();
    connect(host, &CoreHost::notificationRequested, this, [this](const ModuleMsg &msg) {
        m_said << QStringLiteral("new: %1: %2").arg(msg.title, msg.message);
    });
    connect(host, &CoreHost::notificationUpdated, this, [this](const ModuleMsg &msg) {
        m_said << QStringLiteral("update: %1: %2").arg(msg.title, msg.message);
    });
    connect(host, &CoreHost::notificationDismissed, this, [this](const QString &, const QString &) {
        m_said << QStringLiteral("gone");
    });
    m_watcher.reset(new LowStockWatcher(m_core.products.get(), m_core.inventory.get(), nullptr, host));
    m_watcher->check();
}

bool tst_LowStock::article(const QString &code, Quantity stock, Quantity minimum)
{
    Product p;
    p.code = code;
    p.description = QStringLiteral("ARTICULO ") + code;
    p.price = 10000;
    p.cost = 7000;
    p.stock = stock;
    p.stockMin = minimum;
    return m_core.products->saveProduct(p);
}

qint64 tst_LowStock::sell(const QString &code, Quantity quantity)
{
    SaleLine line;
    line.code = code;
    line.description = QStringLiteral("ARTICULO ") + code;
    line.unit = QStringLiteral("un");
    line.unitPrice = 10000;
    line.quantity = quantity;
    SaleDraft draft;
    draft.lines << line;
    return m_core.sales->checkout(draft, { inCash(Money(10000 * quantity / AguaraQuantity::One)) });
}

QStringList tst_LowStock::said()
{
    settle();
    const QStringList result = m_said;
    m_said.clear();
    return result;
}

void tst_LowStock::nothingLowSaysNothing()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(article(QStringLiteral("A"), units(10)));
    QVERIFY(article(QStringLiteral("B"), units(10), 0));        // no minimum: never low
    start();
    QVERIFY(m_core.inventory->adjustStock(QStringLiteral("B"), -units(10), QStringLiteral("rotura")));
    QCOMPARE(said(), QStringList());
}

void tst_LowStock::aSaleThatReachesTheMinimumNotifies()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(article(QStringLiteral("A"), units(10)));
    QVERIFY(m_core.cash->openSession(worth(500000)));
    start();

    QVERIFY(sell(QStringLiteral("A"), units(4)) > 0);           // 6: above it
    QCOMPARE(said(), QStringList());
    QVERIFY(sell(QStringLiteral("A"), units(1)) > 0);           // 5: on it
    QCOMPARE(said(), QStringList { QStringLiteral("new: 1 artículo con stock bajo: ARTICULO A") });
}

void tst_LowStock::onlyAChangeOfTheListSpeaks()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(article(QStringLiteral("A"), units(3)));
    QVERIFY(article(QStringLiteral("B"), units(50)));
    QVERIFY(m_core.cash->openSession(worth(500000)));
    start();
    QCOMPARE(said().size(), 1);

    // every sale refreshes the lists: none of these is news
    QVERIFY(sell(QStringLiteral("A"), units(1)) > 0);           // lower, but low already
    QVERIFY(sell(QStringLiteral("B"), units(1)) > 0);
    m_core.products->refreshAfterChange();
    QCOMPARE(said(), QStringList());

    m_watcher->check();
    QCOMPARE(said(), QStringList());
}

void tst_LowStock::theNewArticleGoesFirst()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    for (const char *code : { "B", "C", "D", "E", "F", "G" })
        QVERIFY(article(QString::fromLatin1(code), units(1)));
    QVERIFY(article(QStringLiteral("Z"), units(10)));
    start();
    QCOMPARE(said().first().count(QStringLiteral("ARTICULO")), 5);

    QVERIFY(m_core.inventory->adjustStock(QStringLiteral("Z"), -units(6), QStringLiteral("rotura")));
    QCOMPARE(said(), QStringList { QStringLiteral(
        "new: 7 artículos con stock bajo: ARTICULO Z, ARTICULO B, ARTICULO C, ARTICULO D, ARTICULO E, ...") });
}

void tst_LowStock::restockingUpdatesQuietly()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(article(QStringLiteral("A"), units(1)));
    QVERIFY(article(QStringLiteral("B"), units(1)));
    start();
    QCOMPARE(said().size(), 1);

    QVERIFY(m_core.inventory->adjustStock(QStringLiteral("A"), units(20), QStringLiteral("llegó")));
    QCOMPARE(said(), QStringList { QStringLiteral("update: 1 artículo con stock bajo: ARTICULO B") });

    // and it counts as new if it falls again
    QVERIFY(m_core.inventory->adjustStock(QStringLiteral("A"), -units(20), QStringLiteral("error")));
    QCOMPARE(said().first().left(5), QStringLiteral("new: "));
}

void tst_LowStock::restockingEverythingTakesItBack()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(article(QStringLiteral("A"), units(1)));
    start();
    QCOMPARE(said().size(), 1);

    QVERIFY(m_core.inventory->adjustStock(QStringLiteral("A"), units(20), QStringLiteral("llegó")));
    QCOMPARE(said(), QStringList { QStringLiteral("gone") });
}

void tst_LowStock::aPurchaseCounts()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(article(QStringLiteral("A"), units(1)));
    start();
    QCOMPARE(said().size(), 1);

    Supplier supplier;
    supplier.name = QStringLiteral("DISTRIBUIDORA");
    Purchase purchase;
    purchase.supplierId = m_core.inventory->saveSupplier(supplier);
    purchase.date = QDate::currentDate();
    purchase.lines = { { QStringLiteral("A"), QString(), units(20), 7000, 0 } };
    QVERIFY2(m_core.inventory->savePurchase(purchase), qPrintable(m_core.inventory->lastError()));
    QCOMPARE(said(), QStringList { QStringLiteral("gone") });
}

void tst_LowStock::aStockCountCounts()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(article(QStringLiteral("A"), units(10)));
    start();

    QList<CountLine> sheet = m_core.inventory->countSheet(0);
    QCOMPARE(sheet.size(), 1);
    sheet[0].counted = units(2);
    sheet[0].wasCounted = true;
    QVERIFY(m_core.inventory->applyCount(sheet, QString()));
    QCOMPARE(said().size(), 1);
}

void tst_LowStock::aReturnCounts()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(article(QStringLiteral("A"), units(10)));
    QVERIFY(m_core.cash->openSession(worth(500000)));
    start();
    const qint64 id = sell(QStringLiteral("A"), units(6));
    QVERIFY(id > 0);
    QCOMPARE(said().size(), 1);

    ReturnRequest request;
    request.saleId = id;
    ReturnLine line;
    line.line = 1;
    line.quantity = units(6);
    request.lines << line;
    request.method = QString::fromLatin1(PaymentMethod::Cash);
    request.reason = QStringLiteral("No le sirvió");
    request.authorizedBy = QStringLiteral("jefe");
    QVERIFY2(m_core.returns->processReturn(request) > 0, qPrintable(m_core.returns->lastError()));
    QCOMPARE(said(), QStringList { QStringLiteral("gone") });
}

void tst_LowStock::voidingCounts()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(article(QStringLiteral("A"), units(10)));
    QVERIFY(m_core.cash->openSession(worth(500000)));
    start();
    const qint64 id = sell(QStringLiteral("A"), units(6));
    QVERIFY(id > 0);
    QCOMPARE(said().size(), 1);

    QVERIFY(m_core.sales->voidSale(id, QStringLiteral("Mal cobrada")));
    QCOMPARE(said(), QStringList { QStringLiteral("gone") });
}

void tst_LowStock::editingTheMinimumCounts()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(article(QStringLiteral("A"), units(10)));
    start();

    Product p = *m_core.products->product(QStringLiteral("A"));
    p.stockMin = units(10);
    QVERIFY(m_core.products->saveProduct(p, p.code));
    QCOMPARE(said().size(), 1);

    p.stockMin = units(9);
    QVERIFY(m_core.products->saveProduct(p, p.code));
    QCOMPARE(said(), QStringList { QStringLiteral("gone") });
}

void tst_LowStock::anArticleOutOfUseLeavesTheList()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(article(QStringLiteral("A"), units(1)));
    start();
    QCOMPARE(said().size(), 1);

    QVERIFY(m_core.products->deactivateProduct(QStringLiteral("A")));
    QCOMPARE(said(), QStringList { QStringLiteral("gone") });
}

void tst_LowStock::anOutageIsNotARestock()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(article(QStringLiteral("A"), units(1)));
    start();
    QCOMPARE(said().size(), 1);

    m_core.monitor->simulateOutage(true);
    m_watcher->check();                     // an empty answer here would take it back
    QCOMPARE(said(), QStringList());
    m_core.monitor->simulateOutage(false);
    m_watcher->check();
    QCOMPARE(said(), QStringList());        // nor is it news when it's back
}

void tst_LowStock::aSaleQueuedInAnOutageCountsWhenItArrives()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(article(QStringLiteral("A"), units(10)));
    QVERIFY(m_core.cash->openSession(worth(500000)));
    start();

    m_core.monitor->simulateOutage(true);
    QVERIFY(sell(QStringLiteral("A"), units(6)) != 0);
    QCOMPARE(said(), QStringList());        // the server doesn't know yet

    m_core.monitor->simulateOutage(false);
    QVERIFY(TestSupport::waitFor([this] { return m_core.sales->pendingSales().isEmpty(); }));
    QCOMPARE(said().size(), 1);
}

QTEST_MAIN(tst_LowStock)
#include "tst_lowstock.moc"
