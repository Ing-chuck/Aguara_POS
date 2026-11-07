// Lots, use-by dates and units of sale, on SQLite and PostgreSQL: every movement of
// stock reaches the lots, the two notifications follow them, and a unit in use stays.
#include "testsupport.h"

#include "expirywatcher.h"

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

class tst_Expiry : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase_data() { TestSupport::addBackendRows(); }
    void cleanup();

    void aNewArticleStartsWithItsLot();
    void theArticleShowsItsSoonestDate();
    void anEditLeavesTheLotsAlone();
    void aNewCodeKeepsTheLots();
    void eachAltaIsItsOwnLot();
    void theSameDateIsOneLot();
    void aSaleTakesTheSoonestDateFirst();
    void sellingWhatWasntLoadedLeavesNoLotBehind();
    void aSaleLeavesExpiredLotsAlone();
    void theLastDayCanStillBeSold();
    void onlyExpiredStockGivesWayToASale();
    void aWriteOffIsntRefilledByAReturn();
    void aVoidPutsItBack();
    void aReturnPutsItBack();
    void anAdjustmentTakesTheSoonestDateFirst();
    void aCountCorrectsTheLots();
    void stockWithNoDateCanGetOne();
    void aWrongDateCanBeTakenAway();

    void noDateNeverNotifies();
    void pastTheDateIsTheHighestLevel();
    void nearTheDateIsAWarning();
    void theLastDayIsStillGood();
    void oneArticleCanBeOnBothLists();
    void lessOfALotUpdatesQuietly();
    void theLotLeavesWhileNewerStockStays();
    void anArticleOutOfUseSaysNothing();
    void theDaysCanBeChanged();
    void theNextDayMovesItToExpired();

    void aUnitIsAddedAndRemoved();
    void aUnitNeedsItsData();
    void aRepeatedNameIsRefused();
    void aUnitInUseStays();
    void theBasicUnitStays();
    void aRemovedUnitComesBack();
    void anArticleOutOfUseDoesntHoldItsUnit();

private:
    void start();
    bool article(const QString &code, const QDate &expires, Quantity stock = AguaraQuantity::whole(5));
    qint64 supplier();
    bool buy(const QString &code, int quantity, const QDate &expires);
    qint64 sell(const QString &code, int quantity);
    /// "2031-01-01:7": each lot with stock, soonest first, in units.
    QStringList lotsOf(const QString &code);
    Quantity units(int count) const { return AguaraQuantity::whole(count); }
    void settle() { QTest::qWait(10); }
    /// What was said since the last call.
    QStringList said();
    QDate inDays(int days) const { return QDate::currentDate().addDays(days); }

    QTemporaryDir m_tmp;
    CoreFixture m_core;
    std::unique_ptr<ExpiryWatcher> m_watcher;
    QStringList m_said;
};

void tst_Expiry::cleanup()
{
    m_watcher.reset();
    m_core.close();
    m_said.clear();
}

void tst_Expiry::start()
{
    CoreHost *host = m_core.host.get();
    connect(host, &CoreHost::notificationRequested, this, [this](const ModuleMsg &msg) {
        m_said << QStringLiteral("new %1 %2 %3").arg(msg.id).arg(int(msg.priority)).arg(msg.title);
    });
    connect(host, &CoreHost::notificationUpdated, this, [this](const ModuleMsg &msg) {
        m_said << QStringLiteral("update %1 %2").arg(msg.id, msg.title);
    });
    connect(host, &CoreHost::notificationDismissed, this, [this](const QString &, const QString &id) {
        m_said << QStringLiteral("gone %1").arg(id);
    });
    m_watcher.reset(new ExpiryWatcher(m_core.products.get(), m_core.inventory.get(), nullptr, host));
    m_watcher->check();
}

bool tst_Expiry::article(const QString &code, const QDate &expires, Quantity stock)
{
    Product p;
    p.code = code;
    p.description = QStringLiteral("ARTICULO ") + code;
    p.price = 10000;
    p.cost = 7000;
    p.stock = stock;
    p.stockExpires = expires;
    return m_core.products->saveProduct(p);
}

qint64 tst_Expiry::supplier()
{
    Supplier s;
    s.name = QStringLiteral("PROVEEDOR");
    return m_core.inventory->saveSupplier(s);
}

bool tst_Expiry::buy(const QString &code, int quantity, const QDate &expires)
{
    PurchaseLine line;
    line.code = code;
    line.quantity = units(quantity);
    line.unitCost = 7000;
    line.expires = expires;
    Purchase purchase;
    const QList<Supplier> known = m_core.inventory->suppliers();
    purchase.supplierId = known.isEmpty() ? supplier() : known.first().id;
    purchase.lines << line;
    return m_core.inventory->savePurchase(purchase) > 0;
}

qint64 tst_Expiry::sell(const QString &code, int quantity)
{
    SaleLine line;
    line.code = code;
    line.description = QStringLiteral("ARTICULO ") + code;
    line.unit = QStringLiteral("un");
    line.unitPrice = 10000;
    line.quantity = units(quantity);
    SaleDraft draft;
    draft.lines << line;
    return m_core.sales->checkout(draft, { inCash(Money(10000 * quantity)) });
}

QStringList tst_Expiry::lotsOf(const QString &code)
{
    QStringList result;
    for (const StockLot &lot : m_core.inventory->lots(code))
        result << QStringLiteral("%1:%2").arg(lot.expires.toString(Qt::ISODate)).arg(AguaraQuantity::format(lot.quantity, 0));
    return result;
}

QStringList tst_Expiry::said()
{
    settle();
    const QStringList result = m_said;
    m_said.clear();
    return result;
}

void tst_Expiry::aNewArticleStartsWithItsLot()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(article(QStringLiteral("A"), QDate(2031, 3, 9)));
    QCOMPARE(lotsOf(QStringLiteral("A")), QStringList(QStringLiteral("2031-03-09:5")));
    QCOMPARE(m_core.products->product(QStringLiteral("A"))->expires, QDate(2031, 3, 9));

    std::unique_ptr<QAbstractItemModel> model(m_core.products->createProductTableModel(nullptr));
    QCOMPARE(model->index(0, int(ProductColumn::Expires)).data().toString(), QStringLiteral("09/03/2031"));
    QVERIFY(!model->index(0, int(ProductColumn::Expires)).data(Qt::ForegroundRole).isValid());

    QVERIFY(article(QStringLiteral("B"), inDays(-1)));
    model.reset(m_core.products->createProductTableModel(nullptr));
    QVERIFY(model->index(1, int(ProductColumn::Expires)).data(Qt::ForegroundRole).isValid());   // red
}

void tst_Expiry::theArticleShowsItsSoonestDate()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(article(QStringLiteral("A"), QDate()));
    QVERIFY(!m_core.products->product(QStringLiteral("A"))->expires.isValid());
    QVERIFY(buy(QStringLiteral("A"), 10, QDate(2032, 1, 1)));
    QVERIFY(buy(QStringLiteral("A"), 10, QDate(2031, 1, 1)));
    QCOMPARE(m_core.products->product(QStringLiteral("A"))->expires, QDate(2031, 1, 1));
}

void tst_Expiry::anEditLeavesTheLotsAlone()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(article(QStringLiteral("A"), QDate(2031, 3, 9)));
    QVERIFY(buy(QStringLiteral("A"), 5, QDate()));
    Product p = *m_core.products->product(QStringLiteral("A"));
    p.description = QStringLiteral("OTRO NOMBRE");
    QVERIFY(m_core.products->saveProduct(p));
    QCOMPARE(lotsOf(QStringLiteral("A")), QStringList(QStringLiteral("2031-03-09:5")));   // the 5 with no date stay so
}

void tst_Expiry::aNewCodeKeepsTheLots()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(article(QStringLiteral("A"), QDate(2031, 3, 9)));
    Product p = *m_core.products->product(QStringLiteral("A"));
    p.code = QStringLiteral("B");
    QVERIFY2(m_core.products->saveProduct(p, QStringLiteral("A")), qPrintable(m_core.products->lastError()));
    QCOMPARE(lotsOf(QStringLiteral("B")), QStringList(QStringLiteral("2031-03-09:5")));
}

void tst_Expiry::eachAltaIsItsOwnLot()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(article(QStringLiteral("A"), QDate()));
    QVERIFY(buy(QStringLiteral("A"), 10, QDate(2032, 1, 1)));
    QVERIFY(buy(QStringLiteral("A"), 7, QDate(2031, 1, 1)));
    QVERIFY(buy(QStringLiteral("A"), 3, QDate()));                  // no date
    QCOMPARE(lotsOf(QStringLiteral("A")), QStringList({ QStringLiteral("2031-01-01:7"), QStringLiteral("2032-01-01:10") }));
    QCOMPARE(m_core.products->product(QStringLiteral("A"))->stock, units(25));  // with the 5 it started with
}

void tst_Expiry::theSameDateIsOneLot()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(article(QStringLiteral("A"), QDate(2031, 1, 1)));
    QVERIFY(buy(QStringLiteral("A"), 10, QDate(2031, 1, 1)));
    QCOMPARE(lotsOf(QStringLiteral("A")), QStringList(QStringLiteral("2031-01-01:15")));
}

void tst_Expiry::aSaleTakesTheSoonestDateFirst()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(article(QStringLiteral("A"), QDate()));                 // 5 with no date
    QVERIFY(buy(QStringLiteral("A"), 10, QDate(2032, 1, 1)));
    QVERIFY(buy(QStringLiteral("A"), 4, QDate(2031, 1, 1)));
    QVERIFY(m_core.cash->openSession(worth(500000)));
    QVERIFY(sell(QStringLiteral("A"), 3) > 0);
    QCOMPARE(lotsOf(QStringLiteral("A")), QStringList({ QStringLiteral("2031-01-01:1"), QStringLiteral("2032-01-01:10") }));
    QVERIFY(sell(QStringLiteral("A"), 3) > 0);                      // across two lots
    QCOMPARE(lotsOf(QStringLiteral("A")), QStringList(QStringLiteral("2032-01-01:8")));
    QVERIFY(sell(QStringLiteral("A"), 10) > 0);                     // the stock with no date goes last
    QCOMPARE(lotsOf(QStringLiteral("A")), QStringList());
    QCOMPARE(m_core.products->product(QStringLiteral("A"))->stock, units(3));
}

void tst_Expiry::sellingWhatWasntLoadedLeavesNoLotBehind()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(article(QStringLiteral("A"), QDate(), 0));
    QVERIFY(m_core.cash->openSession(worth(500000)));
    QVERIFY(sell(QStringLiteral("A"), 3) > 0);
    QCOMPARE(m_core.products->product(QStringLiteral("A"))->stock, -units(3));
    QVERIFY(buy(QStringLiteral("A"), 10, QDate(2031, 1, 1)));       // 3 of them were already sold
    QCOMPARE(lotsOf(QStringLiteral("A")), QStringList(QStringLiteral("2031-01-01:7")));
    QVERIFY(m_core.inventory->addLot(QStringLiteral("A"), units(1), QDate(2031, 2, 2)) == false);   // nothing left with no date
}

void tst_Expiry::aSaleLeavesExpiredLotsAlone()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(article(QStringLiteral("A"), inDays(-3)));              // 5 expired
    QVERIFY(buy(QStringLiteral("A"), 4, inDays(100)));
    QVERIFY(buy(QStringLiteral("A"), 2, QDate()));                  // no date
    start();
    QCOMPARE(said().size(), 1);
    QVERIFY(m_core.cash->openSession(worth(500000)));
    QVERIFY(sell(QStringLiteral("A"), 6) > 0);                      // the good lot, then the stock with no date
    const QString expired = inDays(-3).toString(Qt::ISODate);
    QCOMPARE(lotsOf(QStringLiteral("A")), QStringList(expired + QStringLiteral(":5")));
    QCOMPARE(said(), QStringList());                                // still held, still said
}

void tst_Expiry::theLastDayCanStillBeSold()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(article(QStringLiteral("A"), inDays(0)));
    QVERIFY(m_core.cash->openSession(worth(500000)));
    QVERIFY(sell(QStringLiteral("A"), 2) > 0);
    QCOMPARE(lotsOf(QStringLiteral("A")), QStringList(inDays(0).toString(Qt::ISODate) + QStringLiteral(":3")));
}

void tst_Expiry::onlyExpiredStockGivesWayToASale()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(article(QStringLiteral("A"), inDays(-3)));
    QVERIFY(m_core.cash->openSession(worth(500000)));
    const qint64 id = sell(QStringLiteral("A"), 2);                 // the till asked: this one wasn't expired
    QVERIFY(id > 0);
    QCOMPARE(lotsOf(QStringLiteral("A")), QStringList(inDays(-3).toString(Qt::ISODate) + QStringLiteral(":3")));
    QVERIFY(m_core.sales->voidSale(id, QStringLiteral("Mal cobrada")));
    QCOMPARE(lotsOf(QStringLiteral("A")), QStringList(inDays(-3).toString(Qt::ISODate) + QStringLiteral(":5")));
}

void tst_Expiry::aWriteOffIsntRefilledByAReturn()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(article(QStringLiteral("A"), inDays(-3)));
    QVERIFY(buy(QStringLiteral("A"), 10, inDays(100)));
    QVERIFY(m_core.inventory->adjustStock(QStringLiteral("A"), -units(5), QStringLiteral("vencido, descartado")));
    QCOMPARE(lotsOf(QStringLiteral("A")).size(), 1);
    QVERIFY(m_core.cash->openSession(worth(500000)));
    const qint64 id = sell(QStringLiteral("A"), 3);
    QVERIFY(id > 0);
    QVERIFY(m_core.sales->voidSale(id, QStringLiteral("Mal cobrada")));
    QCOMPARE(lotsOf(QStringLiteral("A")), QStringList(inDays(100).toString(Qt::ISODate) + QStringLiteral(":10")));
}

void tst_Expiry::aVoidPutsItBack()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(article(QStringLiteral("A"), QDate()));
    QVERIFY(buy(QStringLiteral("A"), 4, QDate(2031, 1, 1)));
    QVERIFY(buy(QStringLiteral("A"), 10, QDate(2032, 1, 1)));
    QVERIFY(m_core.cash->openSession(worth(500000)));
    const qint64 id = sell(QStringLiteral("A"), 6);                 // all of the first, 2 of the second
    QVERIFY(id > 0);
    QCOMPARE(lotsOf(QStringLiteral("A")), QStringList(QStringLiteral("2032-01-01:8")));
    QVERIFY(m_core.sales->voidSale(id, QStringLiteral("Mal cobrada")));
    QCOMPARE(lotsOf(QStringLiteral("A")), QStringList({ QStringLiteral("2031-01-01:4"), QStringLiteral("2032-01-01:10") }));
}

void tst_Expiry::aReturnPutsItBack()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(article(QStringLiteral("A"), QDate(2031, 1, 1)));
    QVERIFY(buy(QStringLiteral("A"), 10, QDate()));                 // no date
    QVERIFY(m_core.cash->openSession(worth(500000)));
    const qint64 id = sell(QStringLiteral("A"), 8);                 // the lot's 5, and 3 with no date
    QVERIFY(id > 0);
    QCOMPARE(lotsOf(QStringLiteral("A")), QStringList());

    ReturnRequest request;
    request.saleId = id;
    ReturnLine line;
    line.line = 1;
    line.quantity = units(8);
    request.lines << line;
    request.method = QString::fromLatin1(PaymentMethod::Cash);
    request.reason = QStringLiteral("No le sirvió");
    request.authorizedBy = QStringLiteral("jefe");
    QVERIFY2(m_core.returns->processReturn(request) > 0, qPrintable(m_core.returns->lastError()));
    QCOMPARE(lotsOf(QStringLiteral("A")), QStringList(QStringLiteral("2031-01-01:5")));   // no more than it had
    QCOMPARE(m_core.products->product(QStringLiteral("A"))->stock, units(15));
}

void tst_Expiry::anAdjustmentTakesTheSoonestDateFirst()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(article(QStringLiteral("A"), inDays(-3)));
    QVERIFY(buy(QStringLiteral("A"), 10, inDays(100)));
    QVERIFY(m_core.inventory->adjustStock(QStringLiteral("A"), -units(5), QStringLiteral("vencido, descartado")));
    QCOMPARE(lotsOf(QStringLiteral("A")).size(), 1);                // the expired one is gone
    QCOMPARE(m_core.products->product(QStringLiteral("A"))->expires, inDays(100));

    QVERIFY(m_core.inventory->adjustStock(QStringLiteral("A"), units(5), QStringLiteral("encontrado")));
    QCOMPARE(lotsOf(QStringLiteral("A")).first().section(QLatin1Char(':'), 1), QStringLiteral("10"));     // no date
    QCOMPARE(m_core.products->product(QStringLiteral("A"))->stock, units(15));
}

void tst_Expiry::aCountCorrectsTheLots()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(article(QStringLiteral("A"), QDate(2031, 1, 1)));
    QVERIFY(buy(QStringLiteral("A"), 10, QDate(2032, 1, 1)));
    QList<CountLine> sheet = m_core.inventory->countSheet(0);
    QCOMPARE(sheet.size(), 1);
    sheet[0].counted = units(12);
    sheet[0].wasCounted = true;
    QVERIFY(m_core.inventory->applyCount(sheet, QString()));
    QCOMPARE(lotsOf(QStringLiteral("A")), QStringList({ QStringLiteral("2031-01-01:2"), QStringLiteral("2032-01-01:10") }));
    QCOMPARE(m_core.products->product(QStringLiteral("A"))->stock, units(12));
}

void tst_Expiry::stockWithNoDateCanGetOne()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(article(QStringLiteral("A"), QDate()));
    QVERIFY(!m_core.inventory->addLot(QStringLiteral("A"), units(6), QDate(2031, 1, 1)));  // only 5
    QVERIFY2(m_core.inventory->lastError().contains(QStringLiteral("Solo hay 5")), qPrintable(m_core.inventory->lastError()));
    QVERIFY(!m_core.inventory->addLot(QStringLiteral("A"), 0, QDate(2031, 1, 1)));
    QVERIFY(!m_core.inventory->addLot(QStringLiteral("A"), units(2), QDate()));
    QVERIFY(m_core.inventory->addLot(QStringLiteral("A"), units(2), QDate(2031, 1, 1)));
    QVERIFY(m_core.inventory->addLot(QStringLiteral("A"), units(3), QDate(2031, 2, 2)));
    QCOMPARE(lotsOf(QStringLiteral("A")).size(), 2);
    QVERIFY(!m_core.inventory->addLot(QStringLiteral("A"), units(1), QDate(2031, 3, 3)));
}

void tst_Expiry::aWrongDateCanBeTakenAway()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(article(QStringLiteral("A"), inDays(-3)));
    start();
    QCOMPARE(said().size(), 1);
    const qint64 lot = m_core.inventory->lots(QStringLiteral("A")).first().id;
    QVERIFY(m_core.inventory->removeLot(lot));
    QCOMPARE(said(), QStringList(QStringLiteral("gone vencidos")));
    QCOMPARE(lotsOf(QStringLiteral("A")), QStringList());
    QCOMPARE(m_core.products->product(QStringLiteral("A"))->stock, units(5));

    // and it doesn't come back with a return
    QVERIFY(m_core.inventory->adjustStock(QStringLiteral("A"), -units(1), QStringLiteral("rotura")));
    QVERIFY(!m_core.inventory->removeLot(lot + 100));
}

void tst_Expiry::noDateNeverNotifies()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(article(QStringLiteral("A"), QDate()));
    QVERIFY(article(QStringLiteral("B"), inDays(365)));
    start();
    QCOMPARE(said(), QStringList());
}

void tst_Expiry::pastTheDateIsTheHighestLevel()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(article(QStringLiteral("A"), inDays(-1)));
    start();
    QCOMPARE(said(), QStringList(QStringLiteral("new vencidos %1 1 lote vencido").arg(int(PriorityVeryHigh))));
}

void tst_Expiry::nearTheDateIsAWarning()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(article(QStringLiteral("A"), inDays(30)));
    QVERIFY(article(QStringLiteral("B"), inDays(31)));      // one day too far
    start();
    QCOMPARE(said(), QStringList(QStringLiteral("new por-vencer %1 1 lote por vencer").arg(int(PriorityHigh))));
}

void tst_Expiry::theLastDayIsStillGood()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(article(QStringLiteral("A"), inDays(0)));
    start();
    QCOMPARE(said(), QStringList(QStringLiteral("new por-vencer %1 1 lote por vencer").arg(int(PriorityHigh))));
}

void tst_Expiry::oneArticleCanBeOnBothLists()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(article(QStringLiteral("A"), inDays(-1)));
    QVERIFY(buy(QStringLiteral("A"), 10, inDays(10)));
    QVERIFY(buy(QStringLiteral("A"), 10, inDays(200)));     // this one is fine
    m_core.host->disconnect(this);
    QStringList bodies;
    connect(m_core.host.get(), &CoreHost::notificationRequested, this,
            [&bodies](const ModuleMsg &msg) { bodies << msg.message; });
    ExpiryWatcher watcher(m_core.products.get(), m_core.inventory.get(), nullptr, m_core.host.get());
    watcher.check();
    QCOMPARE(bodies.size(), 2);
    const QString date = inDays(-1).toString(QStringLiteral("dd/MM/yyyy"));
    QVERIFY2(bodies.join(QLatin1Char('|')).contains(QStringLiteral("ARTICULO A, 5 un (%1)").arg(date)), qPrintable(bodies.join(QLatin1Char('|'))));
    QVERIFY(bodies.join(QLatin1Char('|')).contains(QStringLiteral("ARTICULO A, 10 un")));
    m_core.host->disconnect(this);
}

void tst_Expiry::lessOfALotUpdatesQuietly()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(article(QStringLiteral("A"), inDays(-1)));
    start();
    QCOMPARE(said().size(), 1);
    QVERIFY(m_core.inventory->adjustStock(QStringLiteral("A"), -units(1), QStringLiteral("descarte")));
    QCOMPARE(said(), QStringList(QStringLiteral("update vencidos 1 lote vencido")));
}

void tst_Expiry::theLotLeavesWhileNewerStockStays()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(article(QStringLiteral("A"), inDays(-5)));
    QVERIFY(buy(QStringLiteral("A"), 20, inDays(200)));
    start();
    QCOMPARE(said().size(), 1);
    QVERIFY(m_core.inventory->adjustStock(QStringLiteral("A"), -units(5), QStringLiteral("descarte")));
    QCOMPARE(said(), QStringList(QStringLiteral("gone vencidos")));
}

void tst_Expiry::anArticleOutOfUseSaysNothing()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(article(QStringLiteral("A"), inDays(-5)));
    start();
    QCOMPARE(said().size(), 1);
    QVERIFY(m_core.products->deactivateProduct(QStringLiteral("A")));
    QCOMPARE(said(), QStringList(QStringLiteral("gone vencidos")));
}

void tst_Expiry::theDaysCanBeChanged()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QCOMPARE(m_core.inventory->expiryWarningDays(), 30);
    QVERIFY(!m_core.inventory->setExpiryWarningDays(0));
    QVERIFY(!m_core.inventory->setExpiryWarningDays(366));
    QVERIFY(article(QStringLiteral("A"), inDays(60)));
    start();
    QCOMPARE(said(), QStringList());
    QVERIFY(m_core.inventory->setExpiryWarningDays(90));
    QCOMPARE(m_core.inventory->expiryWarningDays(), 90);
    QCOMPARE(said().size(), 1);
    QVERIFY(m_core.inventory->setExpiryWarningDays(10));    // written twice: the second replaces it
    QCOMPARE(m_core.inventory->expiryWarningDays(), 10);
    QCOMPARE(said(), QStringList(QStringLiteral("gone por-vencer")));
}

void tst_Expiry::theNextDayMovesItToExpired()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(article(QStringLiteral("A"), inDays(0)));
    start();
    QCOMPARE(said().size(), 1);
    m_watcher->checkOn(inDays(1));          // a till left open overnight
    const QStringList next = said();
    QCOMPARE(next.size(), 2);
    QVERIFY(next.contains(QStringLiteral("new vencidos %1 1 lote vencido").arg(int(PriorityVeryHigh))));
    QVERIFY(next.contains(QStringLiteral("gone por-vencer")));
}

void tst_Expiry::aUnitIsAddedAndRemoved()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    const int before = m_core.products->units().size();
    StockUnit box;
    box.name = QStringLiteral("  Caja ");
    box.abbreviation = QStringLiteral("cj");
    box.decimals = 0;
    const qint64 id = m_core.products->addUnit(box);
    QVERIFY2(id > 0, qPrintable(m_core.products->lastError()));
    QCOMPARE(m_core.products->units().size(), before + 1);
    QCOMPARE(m_core.products->units().last().name, QStringLiteral("Caja"));
    QCOMPARE(m_core.products->units().first().name, QStringLiteral("Unidad"));

    StockUnit second;
    second.name = QStringLiteral("Galon");
    second.abbreviation = QStringLiteral("gal");
    second.decimals = 2;
    QCOMPARE(m_core.products->addUnit(second), id + 1);

    QVERIFY2(m_core.products->removeUnit(id), qPrintable(m_core.products->lastError()));
    QCOMPARE(m_core.products->units().size(), before + 1);
    QCOMPARE(m_core.products->units().last().name, QStringLiteral("Galon"));
}

void tst_Expiry::aUnitNeedsItsData()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    StockUnit unit;
    QCOMPARE(m_core.products->addUnit(unit), qint64(0));    // no name
    unit.name = QStringLiteral("Caja");
    QCOMPARE(m_core.products->addUnit(unit), qint64(0));    // no abbreviation
    unit.abbreviation = QStringLiteral("cj");
    unit.decimals = 4;
    QCOMPARE(m_core.products->addUnit(unit), qint64(0));    // quantities are in thousandths
    unit.decimals = 3;
    QVERIFY(m_core.products->addUnit(unit) > 0);
}

void tst_Expiry::aRepeatedNameIsRefused()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    StockUnit unit;
    unit.name = QStringLiteral("KILOGRAMO");
    unit.abbreviation = QStringLiteral("kg");
    QCOMPARE(m_core.products->addUnit(unit), qint64(0));
    QVERIFY(m_core.products->lastError().contains(QStringLiteral("Kilogramo")));
}

void tst_Expiry::aUnitInUseStays()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    Product p;
    p.code = QStringLiteral("A");
    p.description = QStringLiteral("QUESO");
    p.price = 10000;
    p.unit.id = 2;      // Kilogramo
    QVERIFY(m_core.products->saveProduct(p));
    QVERIFY(!m_core.products->removeUnit(2));
    QVERIFY2(m_core.products->lastError().contains(QStringLiteral("1 artículo")), qPrintable(m_core.products->lastError()));

    p = *m_core.products->product(QStringLiteral("A"));
    p.unit.id = 1;
    QVERIFY(m_core.products->saveProduct(p));
    QVERIFY(m_core.products->removeUnit(2));
}

void tst_Expiry::theBasicUnitStays()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(!m_core.products->removeUnit(1));
    QCOMPARE(m_core.products->units().first().id, qint64(1));
}

void tst_Expiry::aRemovedUnitComesBack()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(m_core.products->removeUnit(4));    // Metro
    StockUnit unit;
    unit.name = QStringLiteral("metro");
    unit.abbreviation = QStringLiteral("mt");
    unit.decimals = 2;
    QCOMPARE(m_core.products->addUnit(unit), qint64(4));
    const StockUnit back = m_core.products->units().last();
    QCOMPARE(back.id, qint64(4));
    QCOMPARE(back.abbreviation, QStringLiteral("mt"));
    QCOMPARE(back.decimals, 2);
}

void tst_Expiry::anArticleOutOfUseDoesntHoldItsUnit()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    Product p;
    p.code = QStringLiteral("A");
    p.description = QStringLiteral("QUESO");
    p.price = 10000;
    p.unit.id = 2;
    QVERIFY(m_core.products->saveProduct(p));
    QVERIFY(m_core.products->deactivateProduct(QStringLiteral("A")));
    QVERIFY(m_core.products->removeUnit(2));
    const auto kept = m_core.products->product(QStringLiteral("A"));
    QCOMPARE(kept->unit.name, QStringLiteral("Kilogramo"));     // what it had, on the record
}

QTEST_MAIN(tst_Expiry)
#include "tst_expiry.moc"
