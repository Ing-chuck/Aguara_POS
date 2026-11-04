// Returns, credit notes and payment methods: every guaraní that goes back has
// to come from somewhere, and add up.
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

SaleLine lineOf(const QString &code, Money price, Quantity quantity = AguaraQuantity::One, Money discount = 0)
{
    SaleLine line;
    line.code = code;
    line.description = QStringLiteral("ARTICULO ") + code;
    line.unit = QStringLiteral("un");
    line.unitPrice = price;
    line.quantity = quantity;
    line.discount = discount;
    return line;
}

} // namespace

class tst_Returns : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase_data() { TestSupport::addBackendRows(); }

    // finding
    void findsBySaleNumber();
    void unknownSaleIsReported();

    // how much
    void partialReturnGivesItsShare();
    void returnsInPartsAddUpExactly();
    void cantReturnMoreThanSold();
    void needsSomethingAndAReason();
    void saleDiscountIsPartOfTheShare();

    // the money
    void cashLeavesTheSessionAndRounds();
    void fullCashReturnGivesBackWhatCameIn();
    void cardNeedsItsReversalCode();
    void refundIsLimitedToWhatTheMethodBroughtIn();
    void onlyMethodsOfTheSale();
    void drawerMustHaveTheCash();
    void returnOfAnEarlierSessionLeavesThisOne();

    // the rest of the sale
    void stockComesBackAtTheCostOfTheSale();
    void voidedSaleCantBeReturned();
    void returnedSaleCantBeVoided();
    void needsAnOpenSession();
    void needsTheServer();
    void closingShowsTheReturns();

    // credit notes
    void creditNotePaysPartOfASale();
    void creditNoteCantPayMoreThanItsBalance();
    void voidingGivesTheBalanceBack();
    void creditNoteNeedsTheServer();
    void creditNoteKeepsTheCustomer();
    void returnTicketGivesTheNumber();

    // payment methods
    void qrIsAMethodLikeTheOthers();
    void storeAddsItsOwnMethod();
    void methodOutOfUseIsRefused();
    void queuedSaleSurvivesItsMethodLeaving();
    void cashAndCreditNotesArentConfigured();

private:
    bool prepare(Money openingFloat = 500000);
    qint64 sell(const SaleDraft &draft, const QList<SalePayment> &payments);
    qint64 sellOne(Money price, const QList<SalePayment> &payments, Quantity quantity = AguaraQuantity::One);
    ReturnRequest request(qint64 saleId, Quantity quantity, const char *method = PaymentMethod::Cash, int line = 1) const;
    QString number(qint64 saleId) const;
    Money expected();
    Quantity stock(const QString &code = QStringLiteral("A")) const { return m_core.products->product(code)->stock; }

    QTemporaryDir m_tmp;
    CoreFixture m_core;
};

bool tst_Returns::prepare(Money openingFloat)
{
    Product p;
    p.code = QStringLiteral("A");
    p.description = QStringLiteral("ARTICULO A");
    p.price = 10000;
    p.cost = 7000;
    p.stock = AguaraQuantity::whole(100);
    return m_core.products->saveProduct(p) && m_core.cash->openSession(worth(openingFloat));
}

qint64 tst_Returns::sell(const SaleDraft &draft, const QList<SalePayment> &payments)
{
    return m_core.sales->checkout(draft, payments);
}

qint64 tst_Returns::sellOne(Money price, const QList<SalePayment> &payments, Quantity quantity)
{
    SaleDraft draft;
    draft.lines << lineOf(QStringLiteral("A"), price, quantity);
    return sell(draft, payments);
}

ReturnRequest tst_Returns::request(qint64 saleId, Quantity quantity, const char *method, int line) const
{
    ReturnRequest r;
    r.saleId = saleId;
    ReturnLine l;
    l.line = line;
    l.quantity = quantity;
    r.lines << l;
    r.method = QString::fromLatin1(method);
    r.reason = QStringLiteral("No le sirvió");
    r.authorizedBy = QStringLiteral("jefe");
    return r;
}

QString tst_Returns::number(qint64 saleId) const
{
    const auto sale = m_core.sales->sale(saleId);
    return AguaraSale::formatNumber(sale->registerId, sale->number);
}

Money tst_Returns::expected()
{
    return m_core.cash->sessionReport(m_core.cash->currentSession()->id)->expectedCash;
}

void tst_Returns::findsBySaleNumber()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(prepare());
    const qint64 id = sellOne(10000, { inCash(30000) }, AguaraQuantity::whole(3));
    QVERIFY(id > 0);

    const auto sale = m_core.returns->findSale(QStringLiteral("001-000001"));
    QVERIFY2(sale.has_value(), qPrintable(m_core.returns->lastError()));
    QCOMPARE(sale->sale.id, id);
    QCOMPARE(sale->items.size(), 1);
    QCOMPARE(sale->items.first().sold, AguaraQuantity::whole(3));
    QCOMPARE(sale->items.first().returned, Quantity(0));
    QCOMPARE(sale->refundable.size(), 1);
    QCOMPARE(sale->refundable.first().amount, Money(30000));
    QVERIFY(m_core.returns->findSale(QStringLiteral("1")).has_value());     // this register
}

void tst_Returns::unknownSaleIsReported()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(prepare());
    QVERIFY(!m_core.returns->findSale(QStringLiteral("001-000099")));
    QVERIFY(m_core.returns->lastError().contains(QStringLiteral("ninguna venta")));
    QVERIFY(!m_core.returns->findSale(QStringLiteral("hola")));
}

void tst_Returns::partialReturnGivesItsShare()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(prepare());
    const qint64 id = sellOne(10000, { inCash(30000) }, AguaraQuantity::whole(3));

    const qint64 back = m_core.returns->processReturn(request(id, AguaraQuantity::One));
    QVERIFY2(back, qPrintable(m_core.returns->lastError()));
    const auto record = m_core.returns->returnRecord(back);
    QCOMPARE(record->amount, Money(10000));
    QCOMPARE(record->paid, Money(10000));
    QCOMPARE(record->saleNumber, QStringLiteral("001-000001"));
    QCOMPARE(record->items.size(), 1);
    QCOMPARE(record->items.first().vat, Money(909));
    QCOMPARE(stock(), AguaraQuantity::whole(98));

    const auto sale = m_core.returns->findSale(number(id));
    QCOMPARE(sale->items.first().returned, AguaraQuantity::One);
    QCOMPARE(sale->refundable.first().amount, Money(20000));
}

void tst_Returns::returnsInPartsAddUpExactly()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(prepare());
    // 3 at 3.333,33: no share is exact
    SaleDraft draft;
    SaleLine line = lineOf(QStringLiteral("A"), 3333, AguaraQuantity::whole(3));
    line.fixedAmount = 10000;
    draft.lines << line;
    const qint64 id = sell(draft, { byVoucher(PaymentMethod::Debit, 10000, QStringLiteral("D")) });

    Money sum = 0;
    for (int i = 0; i < 3; ++i) {
        ReturnRequest r = request(id, AguaraQuantity::One, PaymentMethod::Debit);
        r.reference = QStringLiteral("R%1").arg(i);
        const qint64 back = m_core.returns->processReturn(r);
        QVERIFY2(back, qPrintable(m_core.returns->lastError()));
        sum += m_core.returns->returnRecord(back)->paid;
    }
    QCOMPARE(sum, Money(10000));                // to the guaraní
    QCOMPARE(m_core.returns->findSale(number(id))->refundable.size(), 0);
}

void tst_Returns::cantReturnMoreThanSold()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(prepare());
    const qint64 id = sellOne(10000, { inCash(20000) }, AguaraQuantity::whole(2));
    QCOMPARE(m_core.returns->processReturn(request(id, AguaraQuantity::whole(3))), qint64(0));
    QVERIFY(m_core.returns->lastError().contains(QStringLiteral("se pueden devolver 2")));

    QVERIFY(m_core.returns->processReturn(request(id, AguaraQuantity::One)));
    QCOMPARE(m_core.returns->processReturn(request(id, AguaraQuantity::whole(2))), qint64(0));
    QVERIFY(m_core.returns->processReturn(request(id, AguaraQuantity::One)));
    QCOMPARE(m_core.returns->processReturn(request(id, AguaraQuantity::One)), qint64(0));
    QCOMPARE(stock(), AguaraQuantity::whole(100));
}

void tst_Returns::needsSomethingAndAReason()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(prepare());
    const qint64 id = sellOne(10000, { inCash(10000) });
    ReturnRequest r = request(id, AguaraQuantity::One);
    r.reason = QStringLiteral("  ");
    QCOMPARE(m_core.returns->processReturn(r), qint64(0));
    QVERIFY(m_core.returns->lastError().contains(QStringLiteral("motivo")));
    QCOMPARE(m_core.returns->processReturn(request(id, 0)), qint64(0));
    QCOMPARE(m_core.returns->processReturn(request(id, -AguaraQuantity::One)), qint64(0));
    QCOMPARE(m_core.returns->processReturn(request(id, AguaraQuantity::One, PaymentMethod::Cash, 9)), qint64(0));
    QCOMPARE(stock(), AguaraQuantity::whole(99));
}

void tst_Returns::saleDiscountIsPartOfTheShare()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(prepare());
    SaleDraft draft;
    draft.lines << lineOf(QStringLiteral("A"), 10000, AguaraQuantity::whole(2));
    draft.totalDiscount = 2000;                 // 18.000 charged: 9.000 each
    const qint64 id = sell(draft, { inCash(18000) });
    const qint64 back = m_core.returns->processReturn(request(id, AguaraQuantity::One));
    QVERIFY(back);
    QCOMPARE(m_core.returns->returnRecord(back)->paid, Money(9000));
}

void tst_Returns::cashLeavesTheSessionAndRounds()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(prepare(500000));
    SaleDraft draft;
    draft.lines << lineOf(QStringLiteral("A"), 4500, 2500);     // 11.250
    const qint64 id = sell(draft, { inCash(11250) });
    QCOMPARE(expected(), Money(511250));

    const qint64 back = m_core.returns->processReturn(request(id, 500));    // a fifth: 2.250
    QVERIFY2(back, qPrintable(m_core.returns->lastError()));
    const auto record = m_core.returns->returnRecord(back);
    QCOMPARE(record->amount, Money(2250));
    QCOMPARE(record->paid, Money(2250));
    QCOMPARE(expected(), Money(509000));

    SaleDraft second;
    second.lines << lineOf(QStringLiteral("A"), 4500, 1250);    // 5.625: 5.650 in cash
    const qint64 other = sell(second, { inCash(5650) });
    const qint64 full = m_core.returns->processReturn(request(other, 1250));
    QCOMPARE(m_core.returns->returnRecord(full)->amount, Money(5625));
    QCOMPARE(m_core.returns->returnRecord(full)->paid, Money(5650));
}

void tst_Returns::fullCashReturnGivesBackWhatCameIn()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(prepare(500000));
    // 5.610 rounds down: the customer paid 5.600, and gets 5.600
    SaleDraft draft;
    SaleLine line = lineOf(QStringLiteral("A"), 5610);
    draft.lines << line;
    const qint64 id = sell(draft, { inCash(5600) });
    QVERIFY(id > 0);
    const qint64 back = m_core.returns->processReturn(request(id, AguaraQuantity::One));
    QVERIFY2(back, qPrintable(m_core.returns->lastError()));
    QCOMPARE(m_core.returns->returnRecord(back)->paid, Money(5600));
    QCOMPARE(expected(), Money(500000));        // the drawer is as it was
}

void tst_Returns::cardNeedsItsReversalCode()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(prepare());
    const qint64 id = sellOne(10000, { byVoucher(PaymentMethod::Credit, 10000, QStringLiteral("C1")) });
    QCOMPARE(m_core.returns->processReturn(request(id, AguaraQuantity::One, PaymentMethod::Credit)), qint64(0));
    QVERIFY(m_core.returns->lastError().contains(QStringLiteral("comprobante")));

    ReturnRequest r = request(id, AguaraQuantity::One, PaymentMethod::Credit);
    r.reference = QStringLiteral(" rev-1 ");
    const qint64 back = m_core.returns->processReturn(r);
    QVERIFY2(back, qPrintable(m_core.returns->lastError()));
    QCOMPARE(m_core.returns->returnRecord(back)->reference, QStringLiteral("REV-1"));
    QCOMPARE(expected(), Money(500000));        // no cash moved
}

void tst_Returns::refundIsLimitedToWhatTheMethodBroughtIn()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(prepare());
    const qint64 id = sellOne(10000, { byVoucher(PaymentMethod::Debit, 15000, QStringLiteral("D1")), inCash(5000) },
                              AguaraQuantity::whole(2));
    // 10.000 back: more than the 5.000 of cash
    QCOMPARE(m_core.returns->processReturn(request(id, AguaraQuantity::One, PaymentMethod::Cash)), qint64(0));
    QVERIFY(m_core.returns->lastError().contains(QStringLiteral("5.000")));

    ReturnRequest r = request(id, AguaraQuantity::One, PaymentMethod::Debit);
    r.reference = QStringLiteral("R1");
    QVERIFY(m_core.returns->processReturn(r));
    // the other 10.000 no longer fit in the 5.000 left on the card
    r.reference = QStringLiteral("R2");
    QCOMPARE(m_core.returns->processReturn(r), qint64(0));
    QVERIFY(m_core.returns->processReturn(request(id, AguaraQuantity::One, PaymentMethod::CreditNote)));
}

void tst_Returns::onlyMethodsOfTheSale()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(prepare());
    const qint64 id = sellOne(10000, { byVoucher(PaymentMethod::Debit, 10000, QStringLiteral("D1")) });
    QCOMPARE(m_core.returns->processReturn(request(id, AguaraQuantity::One, PaymentMethod::Cash)), qint64(0));
    QVERIFY(m_core.returns->lastError().contains(QStringLiteral("no se pagó")));
}

void tst_Returns::drawerMustHaveTheCash()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(prepare(1000));
    const qint64 id = sellOne(10000, { inCash(10000) });
    QVERIFY(m_core.cash->addMovement(CashMovementType::Withdrawal, 9000, QStringLiteral("Al cofre")));
    QCOMPARE(m_core.returns->processReturn(request(id, AguaraQuantity::One)), qint64(0));
    QVERIFY(m_core.returns->lastError().contains(QStringLiteral("efectivo suficiente")));
}

void tst_Returns::returnOfAnEarlierSessionLeavesThisOne()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(prepare(100000));
    const qint64 id = sellOne(10000, { inCash(10000) });
    QCOMPARE(m_core.cash->closeSession(worth(110000)), CloseResult::Closed);

    QVERIFY(m_core.cash->openSession(worth(50000)));
    QVERIFY(m_core.returns->processReturn(request(id, AguaraQuantity::One)));
    QCOMPARE(expected(), Money(40000));
    QCOMPARE(m_core.cash->closeSession(worth(40000)), CloseResult::Closed);
}

void tst_Returns::stockComesBackAtTheCostOfTheSale()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(prepare());
    const qint64 id = sellOne(10000, { inCash(10000) });
    Product p = *m_core.products->product(QStringLiteral("A"));
    p.cost = 9000;                              // it got dearer since
    QVERIFY(m_core.products->saveProduct(p));
    QVERIFY(m_core.returns->processReturn(request(id, AguaraQuantity::One)));

    QSqlQuery q(m_core.connection());
    QVERIFY(q.exec(QStringLiteral("SELECT costo_unitario, cantidad FROM stock_movimientos WHERE tipo = 'devolucion'")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toLongLong(), Money(7000));
    QCOMPARE(q.value(1).toLongLong(), AguaraQuantity::One);
}

void tst_Returns::voidedSaleCantBeReturned()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(prepare());
    const qint64 id = sellOne(10000, { inCash(10000) });
    QVERIFY(m_core.sales->voidSale(id, QStringLiteral("Error")));
    QCOMPARE(m_core.returns->processReturn(request(id, AguaraQuantity::One)), qint64(0));
    QVERIFY(m_core.returns->lastError().contains(QStringLiteral("anulada")));
}

/// Stock and money would come back twice.
void tst_Returns::returnedSaleCantBeVoided()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(prepare());
    const qint64 id = sellOne(10000, { inCash(20000) }, AguaraQuantity::whole(2));
    QVERIFY(m_core.returns->processReturn(request(id, AguaraQuantity::One)));
    QVERIFY(!m_core.sales->voidSale(id, QStringLiteral("Error")));
    QVERIFY(m_core.sales->lastError().contains(QStringLiteral("devoluciones")));
    QCOMPARE(stock(), AguaraQuantity::whole(99));
}

void tst_Returns::needsAnOpenSession()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(prepare(100000));
    const qint64 id = sellOne(10000, { inCash(10000) });
    QCOMPARE(m_core.cash->closeSession(worth(110000)), CloseResult::Closed);
    QCOMPARE(m_core.returns->processReturn(request(id, AguaraQuantity::One)), qint64(0));
    QVERIFY(m_core.returns->lastError().contains(QStringLiteral("Abra la caja")));
}

void tst_Returns::needsTheServer()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(prepare());
    const qint64 id = sellOne(10000, { inCash(10000) });
    m_core.monitor->simulateOutage(true);
    QCOMPARE(m_core.returns->processReturn(request(id, AguaraQuantity::One)), qint64(0));
    QVERIFY(m_core.returns->lastError().contains(QStringLiteral("Sin conexión")));
    m_core.monitor->simulateOutage(false);
}

void tst_Returns::closingShowsTheReturns()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(prepare(100000));
    const qint64 cash = sellOne(10000, { inCash(10000) });
    const qint64 card = sellOne(10000, { byVoucher(PaymentMethod::Debit, 10000, QStringLiteral("D1")) });
    QVERIFY(m_core.returns->processReturn(request(cash, AguaraQuantity::One)));
    ReturnRequest r = request(card, AguaraQuantity::One, PaymentMethod::Debit);
    r.reference = QStringLiteral("R1");
    QVERIFY(m_core.returns->processReturn(r));

    const auto report = m_core.cash->sessionReport(m_core.cash->currentSession()->id);
    QCOMPARE(report->returns.size(), 2);
    QCOMPARE(report->cashRefunds, Money(10000));
    QCOMPARE(report->returns.first().number, QStringLiteral("D-000001"));
    QCOMPARE(report->returns.first().saleNumber, QStringLiteral("001-000001"));
    QCOMPARE(report->returns.first().authorizedBy, QStringLiteral("jefe"));
    QCOMPARE(report->expectedCash, Money(100000));
    QCOMPARE(m_core.cash->closeSession(worth(100000)), CloseResult::Closed);
}

void tst_Returns::creditNotePaysPartOfASale()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(prepare(100000));
    const qint64 id = sellOne(10000, { inCash(10000) });
    const qint64 back = m_core.returns->processReturn(request(id, AguaraQuantity::One, PaymentMethod::CreditNote));
    QVERIFY2(back, qPrintable(m_core.returns->lastError()));
    const QString note = m_core.returns->returnRecord(back)->reference;
    QCOMPARE(note, QStringLiteral("NC-000001"));
    QCOMPARE(expected(), Money(110000));        // no cash left the drawer
    QCOMPARE(m_core.returns->creditNote(note)->balance, Money(10000));

    // pays 6.000 of a sale, the rest stays
    const qint64 first = sellOne(6000, { byVoucher(PaymentMethod::CreditNote, 6000, note) });
    QVERIFY2(first > 0, qPrintable(m_core.sales->lastError()));
    QCOMPARE(m_core.returns->creditNote(QStringLiteral("1"))->balance, Money(4000));

    // with cash on top: only the cash part rounds
    const qint64 second = sellOne(5025, { byVoucher(PaymentMethod::CreditNote, 4000, note), inCash(1050) });
    QVERIFY2(second > 0, qPrintable(m_core.sales->lastError()));
    QCOMPARE(m_core.returns->creditNote(note)->balance, Money(0));
    QCOMPARE(m_core.sales->sale(second)->totals.total, Money(5050));
}

void tst_Returns::creditNoteCantPayMoreThanItsBalance()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(prepare());
    const qint64 id = sellOne(10000, { inCash(10000) });
    const qint64 back = m_core.returns->processReturn(request(id, AguaraQuantity::One, PaymentMethod::CreditNote));
    const QString note = m_core.returns->returnRecord(back)->reference;

    QCOMPARE(sellOne(15000, { byVoucher(PaymentMethod::CreditNote, 15000, note) }), qint64(0));
    QVERIFY(m_core.sales->lastError().contains(QStringLiteral("saldo")));
    QCOMPARE(m_core.returns->creditNote(note)->balance, Money(10000));
    QCOMPARE(m_core.sales->outbox()->count(), 0);       // not charged: not in the queue
    QCOMPARE(stock(), AguaraQuantity::whole(100));

    QCOMPARE(sellOne(5000, { byVoucher(PaymentMethod::CreditNote, 5000, QStringLiteral("NC-999")) }), qint64(0));
}

void tst_Returns::voidingGivesTheBalanceBack()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(prepare());
    const qint64 id = sellOne(10000, { inCash(10000) });
    const qint64 back = m_core.returns->processReturn(request(id, AguaraQuantity::One, PaymentMethod::CreditNote));
    const QString note = m_core.returns->returnRecord(back)->reference;
    const qint64 spent = sellOne(6000, { byVoucher(PaymentMethod::CreditNote, 6000, note) });
    QVERIFY(m_core.sales->voidSale(spent, QStringLiteral("Error")));
    QCOMPARE(m_core.returns->creditNote(note)->balance, Money(10000));
}

void tst_Returns::creditNoteNeedsTheServer()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(prepare());
    const qint64 id = sellOne(10000, { inCash(10000) });
    const qint64 back = m_core.returns->processReturn(request(id, AguaraQuantity::One, PaymentMethod::CreditNote));
    const QString note = m_core.returns->returnRecord(back)->reference;
    m_core.monitor->simulateOutage(true);
    QCOMPARE(sellOne(5000, { byVoucher(PaymentMethod::CreditNote, 5000, note) }), qint64(0));
    QVERIFY(m_core.sales->lastError().contains(QStringLiteral("nota de crédito")));
    m_core.monitor->simulateOutage(false);
    QTest::qWait(0);
}

void tst_Returns::creditNoteKeepsTheCustomer()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(prepare());
    Customer c;
    c.name = QStringLiteral("DOÑA ROSA");
    c.id = m_core.customers->saveCustomer(c);
    SaleDraft draft;
    draft.lines << lineOf(QStringLiteral("A"), 10000);
    draft.customerId = c.id;
    draft.customerName = c.name;
    const qint64 id = sell(draft, { inCash(10000) });
    const qint64 back = m_core.returns->processReturn(request(id, AguaraQuantity::One, PaymentMethod::CreditNote));
    QVERIFY2(back, qPrintable(m_core.returns->lastError()));
    QCOMPARE(m_core.returns->creditNote(m_core.returns->returnRecord(back)->reference)->customerName, c.name);
}

void tst_Returns::returnTicketGivesTheNumber()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(prepare());
    PrinterSettings s;
    s.kind = PrinterKind::Thermal;
    s.name = QStringLiteral("TERMICA");
    m_core.print->savePrinterSettings(s);
    const qint64 id = sellOne(10000, { inCash(20000) }, AguaraQuantity::whole(2));

    const qint64 note = m_core.returns->processReturn(request(id, AguaraQuantity::One, PaymentMethod::CreditNote));
    QVERIFY(m_core.print->finishReturn(*m_core.returns->returnRecord(note)));
    QCOMPARE(m_core.printed.size(), 1);
    QVERIFY(m_core.printed.at(0).contains("NC-000001"));
    QVERIFY(m_core.printed.at(0).contains("D-000001"));
    QVERIFY(!m_core.printed.at(0).contains(QByteArray("\x1B\x70", 2)));     // no cash: the drawer stays shut

    const qint64 cash = m_core.returns->processReturn(request(id, AguaraQuantity::One, PaymentMethod::Cash));
    QVERIFY(m_core.print->finishReturn(*m_core.returns->returnRecord(cash)));
    QVERIFY(m_core.printed.at(1).contains(QByteArray("\x1B\x70", 2)));
}

void tst_Returns::qrIsAMethodLikeTheOthers()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(prepare());
    QStringList codes;
    for (const PaymentMethodInfo &m : m_core.sales->paymentMethods())
        codes << m.code;
    QCOMPARE(codes, QStringList({ "tarjeta_debito", "tarjeta_credito", "transferencia", "qr" }));
    const qint64 id = sellOne(10000, { byVoucher(PaymentMethod::Qr, 10000, QStringLiteral("Q1")) });
    QVERIFY2(id > 0, qPrintable(m_core.sales->lastError()));
    QCOMPARE(AguaraSale::methodName(m_core.sales->sale(id)->payments.first().method), QStringLiteral("QR"));
    QCOMPARE(expected(), Money(500000));
}

void tst_Returns::storeAddsItsOwnMethod()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(prepare());
    PaymentMethodInfo tigo;
    tigo.code = QStringLiteral("  Tigo   Money ");
    tigo.needsCode = false;
    QVERIFY2(m_core.sales->savePaymentMethod(tigo), qPrintable(m_core.sales->lastError()));
    QCOMPARE(m_core.sales->paymentMethods().size(), 5);

    const qint64 id = sellOne(10000, { byVoucher("Tigo Money", 10000, QString()) });      // no code asked
    QVERIFY2(id > 0, qPrintable(m_core.sales->lastError()));
    QCOMPARE(AguaraSale::methodName(m_core.sales->sale(id)->payments.first().method), QStringLiteral("Tigo Money"));

    const auto report = m_core.cash->sessionReport(m_core.cash->currentSession()->id);
    QCOMPARE(report->payments.first().amount, Money(10000));
    QCOMPARE(report->vouchers.size(), 1);
    QCOMPARE(report->expectedCash, Money(500000));
}

void tst_Returns::methodOutOfUseIsRefused()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(prepare());
    PaymentMethodInfo qr;
    qr.code = QString::fromLatin1(PaymentMethod::Qr);
    qr.active = false;
    QVERIFY(m_core.sales->savePaymentMethod(qr));
    QCOMPARE(m_core.sales->paymentMethods().size(), 3);
    QCOMPARE(m_core.sales->paymentMethods(true).size(), 4);
    QCOMPARE(sellOne(10000, { byVoucher(PaymentMethod::Qr, 10000, QStringLiteral("Q1")) }), qint64(0));
    QVERIFY(m_core.sales->lastError().contains(QStringLiteral("no está en uso")));
}

void tst_Returns::queuedSaleSurvivesItsMethodLeaving()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(prepare());
    m_core.sales->paymentMethods();
    m_core.monitor->simulateOutage(true);
    QVERIFY(sellOne(10000, { byVoucher(PaymentMethod::Qr, 10000, QStringLiteral("Q1")) }) < 0);

    PaymentMethodInfo qr;
    qr.code = QString::fromLatin1(PaymentMethod::Qr);
    qr.active = false;
    QVERIFY(m_core.sales->savePaymentMethod(qr));
    m_core.monitor->simulateOutage(false);
    QVERIFY(TestSupport::waitFor([&] { return m_core.sales->pendingSales().isEmpty(); }));   // it was already paid
    QCOMPARE(m_core.sales->sessionSales(m_core.cash->currentSession()->id).size(), 1);
}

void tst_Returns::cashAndCreditNotesArentConfigured()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    PaymentMethodInfo m;
    m.code = QStringLiteral("efectivo");
    QVERIFY(!m_core.sales->savePaymentMethod(m));
    m.code = QStringLiteral("Nota de crédito");
    QVERIFY(!m_core.sales->savePaymentMethod(m));
    m.code = QStringLiteral("   ");
    QVERIFY(!m_core.sales->savePaymentMethod(m));
}

QTEST_MAIN(tst_Returns)
#include "tst_returns.moc"
