// Reports: above all that they agree with each other and with the closing
// sheet, to the guaraní. A report that doesn't match is worse than no report.
#include "sqluserservice.h"
#include "testsupport.h"

#include <QTemporaryDir>
#include <QtTest>

using TestSupport::CoreFixture;
using TestSupport::byVoucher;
using TestSupport::inCash;

namespace {

CashCount worth(Money amount)
{
    DenominationCount coin;
    coin.denomination = 50;
    coin.count = int(amount / 50);
    return { coin };
}

SaleLine lineOf(const QString &code, Money price, Quantity quantity = AguaraQuantity::One, int vat = 10)
{
    SaleLine line;
    line.code = code;
    line.description = QStringLiteral("ARTICULO ") + code;
    line.unit = QStringLiteral("un");
    line.unitPrice = price;
    line.quantity = quantity;
    line.vatRate = vat;
    return line;
}

class FakeProvider : public ReportProvider
{
public:
    QList<ReportDefinition> reports() const override
    {
        ReportDefinition d;
        d.id = QStringLiteral("giros.comisiones");
        d.title = QStringLiteral("Comisiones");
        d.group = QStringLiteral("Giros");
        return { d };
    }
    bool runReport(const QString &, const ReportFilter &, ReportResult *result, QString *) override
    {
        result->columns = { { QStringLiteral("X"), ReportCell::Text } };
        result->rows << QVector<QVariant>{ QStringLiteral("hola") };
        return true;
    }
};

} // namespace

class tst_Reports : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase_data() { TestSupport::addBackendRows(); }

    // they agree
    void dayReportMatchesTheClosingSheet();
    void everyViewOfSalesAddsUp();
    void returnsCountOnTheDayTheyHappen();
    void voidedSalesDontCount();

    // sales
    void periodLimitsWhatIsCounted();
    void filtersByRegisterAndUser();
    void byHourGroupsTheDay();
    void byArticleIsNetOfReturns();
    void categoryIncludesItsChildren();
    void marginUsesTheCostOfTheSale();
    void methodsShowCreditNotes();

    // the till
    void sessionsShowDifferences();
    void openSessionHidesTheExpected();
    void movementsAndVoids();

    // inventory
    void stockIsValued();
    void lowStockMatchesTheService();
    void idleArticles();
    void kardexOfAnArticle();
    void adjustmentsAndPurchases();

    // taxes
    void vatIsNetOfReturns();
    void salesBookListsTickets();

    // audit
    void auditIsFiltered();

    // leaving the program
    void csvIsForExcelInSpanish();
    void csvCantCarryFormulas();
    void cellsAreFormatted();
    void exportsToFiles();
    void longReportsBecomePages();
    void pdfOfNothingAndOfLongText();
    void pdfThatCantBeWrittenSaysSo();
    void modulesAddReports();
    void unknownReportIsReported();
    void everyReportRuns();

private:
    bool add(const QString &code, Money price = 10000, Money cost = 7000, Quantity stock = AguaraQuantity::whole(100));
    qint64 sell(const QList<SaleLine> &lines, const QList<SalePayment> &payments);
    qint64 sellOne(Money price, Money tendered, const QString &code = QStringLiteral("A"));
    ReturnRequest request(qint64 saleId, Quantity quantity = AguaraQuantity::One, const char *method = PaymentMethod::Cash) const;
    ReportFilter today() const;
    ReportResult run(const char *id, const ReportFilter &filter);
    Money cell(const ReportResult &r, const QString &first, int column) const;

    QTemporaryDir m_tmp;
    CoreFixture m_core;
};

bool tst_Reports::add(const QString &code, Money price, Money cost, Quantity stock)
{
    Product p;
    p.code = code;
    p.description = QStringLiteral("ARTICULO ") + code;
    p.price = price;
    p.cost = cost;
    p.stock = stock;
    return m_core.products->saveProduct(p);
}

qint64 tst_Reports::sell(const QList<SaleLine> &lines, const QList<SalePayment> &payments)
{
    SaleDraft draft;
    draft.lines = lines;
    return m_core.sales->checkout(draft, payments);
}

qint64 tst_Reports::sellOne(Money price, Money tendered, const QString &code)
{
    return sell({ lineOf(code, price) }, { inCash(tendered) });
}

ReturnRequest tst_Reports::request(qint64 saleId, Quantity quantity, const char *method) const
{
    ReturnRequest r;
    r.saleId = saleId;
    ReturnLine l;
    l.line = 1;
    l.quantity = quantity;
    r.lines << l;
    r.method = QString::fromLatin1(method);
    r.reason = QStringLiteral("Motivo");
    r.authorizedBy = QStringLiteral("jefe");
    return r;
}

ReportFilter tst_Reports::today() const
{
    ReportFilter f;
    f.from = QDate::currentDate();
    f.to = QDate::currentDate();
    return f;
}

ReportResult tst_Reports::run(const char *id, const ReportFilter &filter)
{
    const auto result = m_core.reports->run(QString::fromLatin1(id), filter);
    if (!result)
        qWarning() << id << m_core.reports->lastError();
    return result.value_or(ReportResult());
}

/// The value in `column` of the row whose first cell is `first`.
Money tst_Reports::cell(const ReportResult &r, const QString &first, int column) const
{
    for (const QVector<QVariant> &row : r.rows) {
        if (row.first().toString() == first)
            return row.at(column).toLongLong();
    }
    return -1;
}

/// The promise of the roadmap: the day's report and the closing sheet are the
/// same number, with rounding, cards, returns and all.
void tst_Reports::dayReportMatchesTheClosingSheet()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("A")));
    QVERIFY(m_core.cash->openSession(worth(100000)));
    const qint64 session = m_core.cash->currentSession()->id;

    QVERIFY(sell({ lineOf(QStringLiteral("A"), 4500, 1250) }, { inCash(10000) }));      // 5.625 rounds to 5.650
    QVERIFY(sell({ lineOf(QStringLiteral("A"), 12500, 1250) },
                 { byVoucher(PaymentMethod::Debit, 10000, QStringLiteral("D")), inCash(6000) }));   // 15.650
    const qint64 returned = sellOne(10000, 10000);
    QVERIFY(m_core.returns->processReturn(request(returned)));
    const qint64 voided = sellOne(3000, 3000);
    QVERIFY(m_core.sales->voidSale(voided, QStringLiteral("Error")));

    const auto sheet = m_core.cash->sessionReport(session);
    const ReportResult day = run("ventas.periodo", today());
    QCOMPARE(day.rows.size(), 1);
    QCOMPARE(day.totals.at(1).toLongLong(), qint64(sheet->salesCount));
    QCOMPARE(day.totals.at(3).toLongLong(), sheet->salesTotal);
    QCOMPARE(day.totals.at(3).toLongLong(), Money(31300));
    QCOMPARE(day.totals.at(4).toLongLong(), Money(10000));

    Money perMethod = 0;
    for (const PaymentTotal &p : sheet->payments)
        perMethod += p.amount;
    QCOMPARE(run("ventas.medio", today()).totals.at(1).toLongLong(), perMethod);

    const ReportResult methods = run("ventas.medio", today());
    QCOMPARE(cell(methods, QStringLiteral("Efectivo"), 3), sheet->expectedCash - sheet->session.openingFloat);
    QCOMPARE(m_core.cash->closeSession(worth(sheet->expectedCash)), CloseResult::Closed);
}

void tst_Reports::everyViewOfSalesAddsUp()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("A")));
    QVERIFY(add(QStringLiteral("B")));
    QVERIFY(m_core.cash->openSession(worth(100000)));
    QVERIFY(sell({ lineOf(QStringLiteral("A"), 10000), lineOf(QStringLiteral("B"), 5000) }, { inCash(15000) }));
    const qint64 id = sellOne(7000, 7000, QStringLiteral("B"));
    QVERIFY(m_core.returns->processReturn(request(id)));

    const ReportFilter f = today();
    const Money net = run("ventas.periodo", f).totals.at(5).toLongLong();
    QCOMPARE(net, Money(15000));
    QCOMPARE(run("ventas.articulo", f).totals.at(3).toLongLong(), net);
    QCOMPARE(run("ventas.rubro", f).totals.at(1).toLongLong(), net);
    QCOMPARE(run("ventas.marca", f).totals.at(1).toLongLong(), net);
    QCOMPARE(run("ventas.cajero", f).totals.at(4).toLongLong(), net);
    QCOMPARE(run("ventas.medio", f).totals.at(3).toLongLong(), net);
    QCOMPARE(run("margen.articulo", f).totals.at(3).toLongLong(), net);
    QCOMPARE(run("margen.rubro", f).totals.at(1).toLongLong(), net);
    QCOMPARE(run("iva.resumen", f).totals.at(1).toLongLong(), net);
}

void tst_Reports::returnsCountOnTheDayTheyHappen()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("A")));
    QVERIFY(m_core.cash->openSession(worth(100000)));
    const qint64 id = sellOne(10000, 10000);
    QVERIFY(m_core.returns->processReturn(request(id)));

    QSqlQuery q(m_core.connection());
    const QString yesterday = QDateTime(QDate::currentDate().addDays(-1), QTime(10, 0)).toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss"));
    QVERIFY(q.exec(QStringLiteral("UPDATE ventas SET fecha = '%1'").arg(yesterday)));

    ReportFilter f;
    f.from = QDate::currentDate().addDays(-1);
    f.to = QDate::currentDate();
    const ReportResult r = run("ventas.periodo", f);
    QCOMPARE(r.rows.size(), 2);
    QCOMPARE(r.rows.at(0).at(3).toLongLong(), Money(10000));
    QCOMPARE(r.rows.at(0).at(4).toLongLong(), Money(0));
    QCOMPARE(r.rows.at(1).at(3).toLongLong(), Money(0));
    QCOMPARE(r.rows.at(1).at(4).toLongLong(), Money(10000));
    QCOMPARE(r.totals.at(5).toLongLong(), Money(0));
}

void tst_Reports::voidedSalesDontCount()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("A")));
    QVERIFY(m_core.cash->openSession(worth(100000)));
    QVERIFY(m_core.sales->voidSale(sellOne(10000, 10000), QStringLiteral("Error")));
    QCOMPARE(run("ventas.periodo", today()).rows.size(), 0);
    QCOMPARE(run("ventas.articulo", today()).rows.size(), 0);
    QCOMPARE(run("iva.libro", today()).rows.size(), 0);
    QCOMPARE(run("caja.anulaciones", today()).rows.size(), 1);
}

void tst_Reports::periodLimitsWhatIsCounted()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("A")));
    QVERIFY(m_core.cash->openSession(worth(100000)));
    QVERIFY(sellOne(10000, 10000));
    ReportFilter past;
    past.from = QDate::currentDate().addDays(-10);
    past.to = QDate::currentDate().addDays(-1);
    QCOMPARE(run("ventas.periodo", past).rows.size(), 0);
    QCOMPARE(run("ventas.periodo", today()).rows.size(), 1);    // the whole of today, to its last second
}

void tst_Reports::filtersByRegisterAndUser()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    SqlUserService users(m_core.monitor.get());
    QVERIFY(users.createFirstAdmin(QStringLiteral("admin"), QStringLiteral("Admin"), QStringLiteral("secreto1")));
    m_core.sales->setUsers(&users);
    QVERIFY(add(QStringLiteral("A")));
    QVERIFY(m_core.cash->openSession(worth(100000)));
    QVERIFY(sellOne(10000, 10000));

    ReportFilter f = today();
    f.user = QStringLiteral("admin");
    QCOMPARE(run("ventas.periodo", f).rows.size(), 1);
    f.user = QStringLiteral("nadie");
    QCOMPARE(run("ventas.periodo", f).rows.size(), 0);
    f.user.clear();
    f.registerId = m_core.cash->currentRegister().id;
    QCOMPARE(run("ventas.periodo", f).rows.size(), 1);
    f.registerId = 999;
    QCOMPARE(run("ventas.periodo", f).rows.size(), 0);
    QCOMPARE(m_core.reports->registers().size(), 1);
    QCOMPARE(m_core.reports->sellers().first().name, QStringLiteral("admin"));
    m_core.sales->setUsers(nullptr);
}

void tst_Reports::byHourGroupsTheDay()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("A")));
    QVERIFY(m_core.cash->openSession(worth(100000)));
    QVERIFY(sellOne(10000, 10000));
    QVERIFY(sellOne(5000, 5000));
    const ReportResult r = run("ventas.hora", today());
    QVERIFY(r.rows.size() >= 1);
    QCOMPARE(r.rows.first().at(0).toString(), QTime::currentTime().toString(QStringLiteral("HH")) + QStringLiteral(":00"));
    QCOMPARE(r.totals.at(1).toLongLong(), qint64(2));
    QCOMPARE(r.totals.at(2).toLongLong(), Money(15000));
}

void tst_Reports::byArticleIsNetOfReturns()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("A")));
    QVERIFY(add(QStringLiteral("B")));
    QVERIFY(m_core.cash->openSession(worth(100000)));
    const qint64 id = sell({ lineOf(QStringLiteral("A"), 10000, AguaraQuantity::whole(3)) }, { inCash(30000) });
    QVERIFY(sellOne(20000, 20000, QStringLiteral("B")));
    QVERIFY(m_core.returns->processReturn(request(id)));

    const ReportResult r = run("ventas.articulo", today());
    QCOMPARE(r.rows.size(), 2);
    QCOMPARE(r.rows.at(0).at(3).toLongLong(), Money(20000));    // ordered by money; tie by code
    QCOMPARE(cell(r, QStringLiteral("A"), 2), AguaraQuantity::whole(2));
    QCOMPARE(cell(r, QStringLiteral("A"), 3), Money(20000));
}

void tst_Reports::categoryIncludesItsChildren()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    const qint64 parent = m_core.products->addCategory(QStringLiteral("ALMACEN"));
    QVERIFY(parent);
    const qint64 child = m_core.products->addCategory(QStringLiteral("LACTEOS"), parent);
    QVERIFY(child);
    QVERIFY(add(QStringLiteral("A")));
    QVERIFY(add(QStringLiteral("B")));
    Product p = *m_core.products->product(QStringLiteral("A"));
    p.categoryId = child;
    QVERIFY(m_core.products->saveProduct(p));
    QVERIFY(m_core.cash->openSession(worth(100000)));
    QVERIFY(sell({ lineOf(QStringLiteral("A"), 10000), lineOf(QStringLiteral("B"), 5000) }, { inCash(15000) }));

    ReportFilter f = today();
    f.categoryId = parent;
    const ReportResult r = run("ventas.articulo", f);
    QCOMPARE(r.rows.size(), 1);
    QCOMPARE(r.rows.first().at(0).toString(), QStringLiteral("A"));
    QCOMPARE(run("stock.valorizado", f).rows.size(), 1);
}

void tst_Reports::marginUsesTheCostOfTheSale()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("A"), 10000, 7000));
    QVERIFY(m_core.cash->openSession(worth(100000)));
    QVERIFY(sellOne(10000, 10000));
    Product p = *m_core.products->product(QStringLiteral("A"));
    p.cost = 9500;                              // it got dearer after the sale
    QVERIFY(m_core.products->saveProduct(p));

    const ReportResult r = run("margen.articulo", today());
    QCOMPARE(r.totals.at(3).toLongLong(), Money(10000));
    QCOMPARE(r.totals.at(4).toLongLong(), Money(7000));
    QCOMPARE(r.totals.at(5).toLongLong(), Money(3000));
    QCOMPARE(r.totals.at(6).toDouble(), 30.0);
}

void tst_Reports::methodsShowCreditNotes()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("A")));
    QVERIFY(m_core.cash->openSession(worth(100000)));
    const qint64 id = sellOne(10000, 20000);                    // 10.000 of change: not income
    const qint64 back = m_core.returns->processReturn(request(id, AguaraQuantity::One, PaymentMethod::CreditNote));
    const QString note = m_core.returns->returnRecord(back)->reference;
    QVERIFY(sell({ lineOf(QStringLiteral("A"), 4000) }, { byVoucher(PaymentMethod::CreditNote, 4000, note) }) > 0);

    const ReportResult r = run("ventas.medio", today());
    QCOMPARE(cell(r, QStringLiteral("Efectivo"), 1), Money(10000));
    QCOMPARE(cell(r, QStringLiteral("Nota de crédito"), 1), Money(4000));
    QCOMPARE(cell(r, QStringLiteral("Nota de crédito"), 2), Money(10000));
}

void tst_Reports::sessionsShowDifferences()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("A")));
    QVERIFY(m_core.cash->openSession(worth(100000)));
    QVERIFY(sellOne(10000, 10000));
    QVERIFY(m_core.cash->closeWithDifference(worth(105000), QStringLiteral("Faltó"), QStringLiteral("jefe")));

    const ReportResult r = run("caja.sesiones", today());
    QCOMPARE(r.rows.size(), 1);
    const auto row = r.rows.first();
    QCOMPARE(row.at(6).toLongLong(), Money(110000));
    QCOMPARE(row.at(7).toLongLong(), Money(105000));
    QCOMPARE(row.at(8).toLongLong(), Money(-5000));
    QCOMPARE(row.at(9).toString(), QStringLiteral("Faltó"));
    QCOMPARE(row.at(10).toString(), QStringLiteral("jefe"));
    QCOMPARE(r.totals.at(8).toLongLong(), Money(-5000));
}

/// The count is blind: no report may give the expected cash away.
void tst_Reports::openSessionHidesTheExpected()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("A")));
    QVERIFY(m_core.cash->openSession(worth(100000)));
    QVERIFY(sellOne(10000, 10000));
    const auto row = run("caja.sesiones", today()).rows.first();
    QVERIFY(row.at(3).isNull());
    QVERIFY(row.at(6).isNull());
    QVERIFY(row.at(8).isNull());
}

void tst_Reports::movementsAndVoids()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("A")));
    QVERIFY(m_core.cash->openSession(worth(100000)));
    QVERIFY(m_core.cash->addMovement(CashMovementType::In, 20000, QStringLiteral("Cambio")));
    QVERIFY(m_core.cash->addMovement(CashMovementType::Withdrawal, 5000, QStringLiteral("Al cofre")));
    const ReportResult moves = run("caja.movimientos", today());
    QCOMPARE(moves.rows.size(), 2);
    QCOMPARE(moves.totals.at(3).toLongLong(), Money(15000));

    const qint64 a = sellOne(10000, 10000);
    const qint64 b = sellOne(3000, 3000);
    QVERIFY(m_core.sales->voidSale(b, QStringLiteral("Mal cobrado")));
    QVERIFY(m_core.returns->processReturn(request(a)));
    const ReportResult list = run("caja.anulaciones", today());
    QCOMPARE(list.rows.size(), 2);
    QCOMPARE(list.totals.at(3).toLongLong(), Money(13000));
    QStringList kinds = { list.rows.at(0).at(1).toString(), list.rows.at(1).at(1).toString() };
    kinds.sort();
    QCOMPARE(kinds, QStringList({ "Anulación", "Devolución" }));
}

void tst_Reports::stockIsValued()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("A"), 10000, 7000, AguaraQuantity::whole(3)));
    QVERIFY(add(QStringLiteral("B"), 2000, 1500, 2500));        // 2,5 kg
    const ReportResult r = run("stock.valorizado", ReportFilter());
    QCOMPARE(r.rows.size(), 2);
    QCOMPARE(r.totals.at(6).toLongLong(), Money(21000 + 3750));
    QCOMPARE(r.totals.at(7).toLongLong(), Money(30000 + 5000));
}

void tst_Reports::lowStockMatchesTheService()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("A"), 10000, 7000, AguaraQuantity::whole(2)));
    Product p = *m_core.products->product(QStringLiteral("A"));
    p.stockMin = AguaraQuantity::whole(5);
    QVERIFY(m_core.products->saveProduct(p));
    QVERIFY(add(QStringLiteral("B")));
    const ReportResult r = run("stock.minimo", ReportFilter());
    QCOMPARE(r.rows.size(), m_core.inventory->lowStock().size());
    QCOMPARE(r.rows.first().at(5).toLongLong(), AguaraQuantity::whole(3));
}

void tst_Reports::idleArticles()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("A")));
    QVERIFY(add(QStringLiteral("B")));
    QSqlQuery q(m_core.connection());
    const QString old = QDateTime(QDate::currentDate().addDays(-60), QTime(10, 0)).toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss"));
    QVERIFY(q.exec(QStringLiteral("UPDATE stock_movimientos SET fecha = '%1' WHERE producto_codigo = 'A'").arg(old)));

    ReportFilter f;
    f.days = 30;
    const ReportResult r = run("stock.sin_movimiento", f);
    QCOMPARE(r.rows.size(), 1);
    QCOMPARE(r.rows.first().at(0).toString(), QStringLiteral("A"));
    QCOMPARE(r.rows.first().at(4).toDateTime().date(), QDate::currentDate().addDays(-60));
    f.days = 90;
    QCOMPARE(run("stock.sin_movimiento", f).rows.size(), 0);
}

void tst_Reports::kardexOfAnArticle()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("A"), 10000, 7000, AguaraQuantity::whole(10)));
    QVERIFY(m_core.cash->openSession(worth(100000)));
    QVERIFY(sellOne(10000, 10000));

    ReportFilter f = today();
    f.text = QStringLiteral("A");
    const ReportResult r = run("stock.kardex", f);
    QVERIFY(r.rows.size() >= 2);
    QCOMPARE(r.rows.last().at(1).toString(), QStringLiteral("venta"));
    QCOMPARE(r.rows.last().at(2).toLongLong(), -AguaraQuantity::One);
    QCOMPARE(r.rows.last().at(3).toLongLong(), AguaraQuantity::whole(9));

    f.text = QStringLiteral("NO-EXISTE");
    QVERIFY(!m_core.reports->run(QStringLiteral("stock.kardex"), f));
    QVERIFY(m_core.reports->lastError().contains(QStringLiteral("código")));
}

void tst_Reports::adjustmentsAndPurchases()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("A"), 10000, 7000, AguaraQuantity::whole(10)));
    QVERIFY(m_core.inventory->adjustStock(QStringLiteral("A"), -AguaraQuantity::whole(2), QStringLiteral("Rotura")));
    const ReportResult adjusted = run("stock.ajustes", today());
    QCOMPARE(adjusted.rows.size(), 1);
    QCOMPARE(adjusted.totals.at(5).toLongLong(), Money(-14000));

    Supplier s;
    s.name = QStringLiteral("DISTRIBUIDORA");
    const qint64 supplier = m_core.inventory->saveSupplier(s);
    QVERIFY(supplier);
    Purchase purchase;
    purchase.supplierId = supplier;
    purchase.date = QDate::currentDate();
    PurchaseLine line;
    line.code = QStringLiteral("A");
    line.quantity = AguaraQuantity::whole(2);
    line.unitCost = 7000;
    purchase.lines << line;
    QVERIFY2(m_core.inventory->savePurchase(purchase), qPrintable(m_core.inventory->lastError()));
    const ReportResult bought = run("stock.compras", today());
    QCOMPARE(bought.rows.size(), 1);
    QCOMPARE(bought.rows.first().at(0).toString(), QStringLiteral("DISTRIBUIDORA"));
    QCOMPARE(bought.totals.at(2).toLongLong(), Money(14000));
}

void tst_Reports::vatIsNetOfReturns()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("A")));
    QVERIFY(m_core.cash->openSession(worth(100000)));
    const qint64 ten = sell({ lineOf(QStringLiteral("A"), 11000, AguaraQuantity::whole(2), 10) }, { inCash(22000) });
    QVERIFY(sell({ lineOf(QStringLiteral("A"), 10500, AguaraQuantity::One, 5), lineOf(QStringLiteral("A"), 3000, AguaraQuantity::One, 0) },
                 { inCash(13500) }));
    QVERIFY(m_core.returns->processReturn(request(ten)));       // 11.000, 1.000 of IVA

    const ReportResult r = run("iva.resumen", today());
    QCOMPARE(cell(r, QStringLiteral("10 %"), 1), Money(11000));
    QCOMPARE(cell(r, QStringLiteral("10 %"), 2), Money(1000));
    QCOMPARE(cell(r, QStringLiteral("10 %"), 3), Money(10000));
    QCOMPARE(cell(r, QStringLiteral("5 %"), 2), Money(500));
    QCOMPARE(cell(r, QStringLiteral("Exentas"), 1), Money(3000));
    QCOMPARE(r.totals.at(1).toLongLong(), Money(24500));
    QCOMPARE(r.totals.at(2).toLongLong(), Money(1500));
}

void tst_Reports::salesBookListsTickets()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("A")));
    QVERIFY(m_core.cash->openSession(worth(100000)));
    Customer c;
    c.name = QStringLiteral("ANDE");
    c.documentType = QString::fromLatin1(DocumentType::Ruc);
    c.document = QStringLiteral("80009735-1");
    c.id = m_core.customers->saveCustomer(c);
    SaleDraft draft;
    draft.lines << lineOf(QStringLiteral("A"), 11000);
    draft.customerId = c.id;
    draft.customerName = c.name;
    draft.customerDocument = c.document;
    QVERIFY(m_core.sales->checkout(draft, { inCash(11000) }));
    QVERIFY(sellOne(5000, 5000));

    const ReportResult r = run("iva.libro", today());
    QCOMPARE(r.rows.size(), 2);
    QCOMPARE(r.rows.at(0).at(1).toString(), QStringLiteral("001-000001"));
    QCOMPARE(r.rows.at(0).at(2).toString(), QStringLiteral("ANDE"));
    QCOMPARE(r.rows.at(0).at(3).toString(), QStringLiteral("80009735-1"));
    QCOMPARE(r.rows.at(0).at(5).toLongLong(), Money(1000));
    QCOMPARE(r.rows.at(1).at(2).toString(), QStringLiteral("Consumidor final"));
    QCOMPARE(r.totals.at(9).toLongLong(), Money(16000));
    QVERIFY(r.note.contains(QStringLiteral("Borrador")));
}

void tst_Reports::auditIsFiltered()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    SqlUserService users(m_core.monitor.get());
    QVERIFY(users.createFirstAdmin(QStringLiteral("admin"), QStringLiteral("Admin"), QStringLiteral("secreto1")));
    m_core.reports->setUsers(&users);
    users.audit(QStringLiteral("prueba.una"), QStringLiteral("cosa"), QStringLiteral("1"));
    users.audit(QStringLiteral("prueba.otra"), QStringLiteral("cosa"), QStringLiteral("2"));

    ReportFilter f = today();
    f.text = QStringLiteral("prueba.una");
    const ReportResult r = run("auditoria", f);
    QCOMPARE(r.rows.size(), 1);
    QCOMPARE(r.rows.first().at(4).toString(), QStringLiteral("prueba.una"));
    QCOMPARE(r.rows.first().at(5).toString(), QStringLiteral("cosa 1"));
    f.text.clear();
    f.user = QStringLiteral("nadie");
    QCOMPARE(run("auditoria", f).rows.size(), 0);
    m_core.reports->setUsers(nullptr);
}

void tst_Reports::csvIsForExcelInSpanish()
{
    ReportResult r;
    r.columns = { { QStringLiteral("Artículo"), ReportCell::Text }, { QStringLiteral("Cantidad"), ReportCell::Quantity },
                  { QStringLiteral("Importe"), ReportCell::Money }, { QStringLiteral("%"), ReportCell::Percent } };
    r.rows << QVector<QVariant>{ QStringLiteral("Leche; \"entera\""), Quantity(2500), Money(1234567), 12.5 };
    r.totals = { QStringLiteral("Total"), QVariant(), Money(1234567), QVariant() };
    const QByteArray csv = AguaraReport::toCsv(r);
    QVERIFY(csv.startsWith("\xEF\xBB\xBF"));    // or Excel breaks the accents
    const QStringList lines = QString::fromUtf8(csv.mid(3)).split(QStringLiteral("\r\n"));
    QCOMPARE(lines.at(0), QStringLiteral("Artículo;Cantidad;Importe;%"));
    QCOMPARE(lines.at(1), QStringLiteral("\"Leche; \"\"entera\"\"\";2,500;1234567;12,5"));   // a number, not a text
    QCOMPARE(lines.at(2), QStringLiteral("Total;;1234567;"));
}

void tst_Reports::csvCantCarryFormulas()
{
    ReportResult r;
    r.columns = { { QStringLiteral("Artículo"), ReportCell::Text }, { QStringLiteral("Monto"), ReportCell::Money } };
    r.rows << QVector<QVariant>{ QStringLiteral("=HYPERLINK(\"x\")"), Money(-500) };
    const QString csv = QString::fromUtf8(AguaraReport::toCsv(r));
    QVERIFY(csv.contains(QStringLiteral("'=HYPERLINK")));
    QVERIFY(csv.contains(QStringLiteral(";-500")));             // a negative amount stays a number
}

void tst_Reports::cellsAreFormatted()
{
    QCOMPARE(AguaraReport::formatCell(ReportCell::Money, Money(1234567)), QStringLiteral("1.234.567"));
    QCOMPARE(AguaraReport::formatCell(ReportCell::Quantity, AguaraQuantity::whole(3)), QStringLiteral("3"));
    QCOMPARE(AguaraReport::formatCell(ReportCell::Quantity, Quantity(1250)), QStringLiteral("1,250"));
    QCOMPARE(AguaraReport::formatCell(ReportCell::Percent, 12.5), QStringLiteral("12,5 %"));
    QCOMPARE(AguaraReport::formatCell(ReportCell::Date, QDate(2026, 11, 4)), QStringLiteral("04/11/2026"));
    QVERIFY(AguaraReport::formatCell(ReportCell::Money, QVariant()).isEmpty());
    QCOMPARE(AguaraReport::formatCell(ReportCell::Date, QStringLiteral("Total")), QStringLiteral("Total"));
}

void tst_Reports::exportsToFiles()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("A")));
    const ReportResult r = run("stock.valorizado", ReportFilter());

    const QString csv = m_tmp.filePath(QStringLiteral("reporte.csv"));
    QVERIFY2(m_core.reports->exportCsv(r, csv), qPrintable(m_core.reports->lastError()));
    QVERIFY(QFile(csv).size() > 3);

    const QString pdf = m_tmp.filePath(QStringLiteral("reporte.pdf"));
    QVERIFY2(m_core.reports->exportPdf(r, pdf), qPrintable(m_core.reports->lastError()));
    QFile file(pdf);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QVERIFY(file.read(4) == "%PDF");
    file.close();
    QVERIFY(QFile::remove(pdf));
    QVERIFY(QFile::remove(csv));

    QVERIFY(!m_core.reports->exportCsv(r, QStringLiteral("Z:/no/existe/aqui.csv")));
}

/// A built report: no database needed to know how it goes on paper.
static ReportResult sampleReport(int rows)
{
    ReportResult r;
    r.title = QStringLiteral("Libro de ventas");
    r.subtitle = QStringLiteral("Del 01/01/2025 al 31/12/2026");
    r.note = QStringLiteral("Borrador");
    r.columns = { { QStringLiteral("Fecha"), ReportCell::Date }, { QStringLiteral("Cliente"), ReportCell::Text },
                  { QStringLiteral("Cantidad"), ReportCell::Quantity }, { QStringLiteral("Total"), ReportCell::Money },
                  { QStringLiteral("Margen"), ReportCell::Percent }, { QStringLiteral("Items"), ReportCell::Integer } };
    for (int i = 0; i < rows; ++i)
        r.rows << QVector<QVariant>{ QDate(2026, 1, 1 + i % 28), QStringLiteral("Cliente número %1").arg(i),
                                     qint64(AguaraQuantity::whole(i % 9 + 1)), qint64(10000 + i), 27.5, i % 40 };
    r.totals = { QStringLiteral("Total"), QVariant(), QVariant(), qint64(rows) * 10000, QVariant(), QVariant() };
    return r;
}

static int pagesOf(const QString &pdf)
{
    QFile file(pdf);
    if (!file.open(QIODevice::ReadOnly))
        return -1;
    const QByteArray bytes = file.readAll();
    return bytes.count("/Type /Page\n") + bytes.count("/Type /Page\r") + bytes.count("/Type /Page/")
         + bytes.count("/Type /Page>") + bytes.count("/Type /Page ");
}

void tst_Reports::longReportsBecomePages()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    const QString pdf = m_tmp.filePath(QStringLiteral("largo.pdf"));

    // With HTML this took minutes, and 177.000 rows ran the program out of memory
    QElapsedTimer timer;
    timer.start();
    QVERIFY2(m_core.reports->exportPdf(sampleReport(30000), pdf), qPrintable(m_core.reports->lastError()));
    QVERIFY2(timer.elapsed() < 30000, qPrintable(QStringLiteral("%1 ms").arg(timer.elapsed())));

    const int many = pagesOf(pdf);
    QVERIFY2(many > 300, qPrintable(QString::number(many)));
    QVERIFY(m_core.reports->exportPdf(sampleReport(20), pdf));
    QCOMPARE(pagesOf(pdf), 1);
    QVERIFY(m_core.reports->exportPdf(sampleReport(40), pdf));      // on its side, a page takes some 28
    QCOMPARE(pagesOf(pdf), 2);
    QVERIFY(QFile::remove(pdf));
}

void tst_Reports::pdfOfNothingAndOfLongText()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    const QString pdf = m_tmp.filePath(QStringLiteral("borde.pdf"));

    ReportResult empty = sampleReport(0);
    empty.totals.clear();
    QVERIFY(m_core.reports->exportPdf(empty, pdf));
    QCOMPARE(pagesOf(pdf), 1);

    ReportResult bare;                      // nothing at all, not even columns
    bare.title = QStringLiteral("Vacío");
    QVERIFY(m_core.reports->exportPdf(bare, pdf));

    // text longer than any column, than a page, and no spaces to break it at
    ReportResult wide = sampleReport(3);
    wide.rows[0][1] = QString(20000, QLatin1Char('x'));
    wide.rows[1][1] = QStringLiteral("palabra ").repeated(5000);
    QVERIFY(m_core.reports->exportPdf(wide, pdf));
    QVERIFY(pagesOf(pdf) >= 1);

    // rows shorter than the columns, as a module's report could send
    ReportResult shorter = sampleReport(1);
    shorter.rows[0].resize(2);
    QVERIFY(m_core.reports->exportPdf(shorter, pdf));
    QVERIFY(QFile::remove(pdf));
}

void tst_Reports::pdfThatCantBeWrittenSaysSo()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(!m_core.reports->exportPdf(sampleReport(5), QStringLiteral("Z:/no/existe/aqui.pdf")));
    QVERIFY(!m_core.reports->lastError().isEmpty());
}

void tst_Reports::modulesAddReports()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    FakeProvider provider;
    m_core.reports->addProvider(&provider);
    const QList<ReportDefinition> all = m_core.reports->reports();
    QVERIFY(std::any_of(all.cbegin(), all.cend(),
                        [](const ReportDefinition &d) { return d.id == QLatin1String("giros.comisiones"); }));
    const auto r = m_core.reports->run(QStringLiteral("giros.comisiones"), ReportFilter());
    QVERIFY(r.has_value());
    QCOMPARE(r->title, QStringLiteral("Comisiones"));
    QCOMPARE(r->rows.first().first().toString(), QStringLiteral("hola"));
}

void tst_Reports::unknownReportIsReported()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(!m_core.reports->run(QStringLiteral("no.existe"), ReportFilter()));
}

/// Every query runs on both databases, with every filter on at once.
void tst_Reports::everyReportRuns()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("A")));
    QVERIFY(m_core.cash->openSession(worth(100000)));
    QVERIFY(sellOne(10000, 10000));

    const qint64 category = m_core.products->addCategory(QStringLiteral("ALMACEN"));
    ReportFilter f = today();
    f.registerId = m_core.cash->currentRegister().id;
    f.user = QStringLiteral("alguien");
    f.categoryId = category;
    f.text = QStringLiteral("A");
    const QList<ReportDefinition> all = m_core.reports->reports();
    QVERIFY(all.size() >= 21);
    for (const ReportDefinition &d : all) {
        const auto result = m_core.reports->run(d.id, f);
        QVERIFY2(result.has_value(), qPrintable(d.id + QStringLiteral(": ") + m_core.reports->lastError()));
        QVERIFY2(!result->columns.isEmpty(), qPrintable(d.id));
        QCOMPARE(result->title, d.title);
        for (const QVector<QVariant> &row : result->rows)
            QCOMPARE(row.size(), result->columns.size());
        if (!result->totals.isEmpty())
            QCOMPARE(result->totals.size(), result->columns.size());
    }
}

QTEST_MAIN(tst_Reports)
#include "tst_reports.moc"
