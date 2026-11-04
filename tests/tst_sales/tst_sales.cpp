// Cash sessions and sales: what a day at the till leaves in the database, on
// SQLite and PostgreSQL.
#include "testsupport.h"

#include <cashservice.h>
#include <salesservice.h>

#include <QtTest>

using TestSupport::CoreFixture;
using TestSupport::inCash;
using TestSupport::byVoucher;

namespace {

CashCount notes(Money denomination, int count)
{
    DenominationCount d;
    d.denomination = denomination;
    d.count = count;
    return { d };
}

/// A count worth exactly `amount`, in 50 Gs. coins: only the sum matters here.
CashCount worth(Money amount)
{
    return notes(50, int(amount / 50));
}

Money net(const SaleDraft &draft)
{
    return AguaraSale::totals(draft).net;
}

SaleLine lineOf(const QString &code, Money price, Quantity quantity = AguaraQuantity::One, int vat = 10)
{
    SaleLine l;
    l.code = code;
    l.description = QStringLiteral("ARTICULO ") + code;
    l.unit = QStringLiteral("un");
    l.unitPrice = price;
    l.quantity = quantity;
    l.vatRate = vat;
    return l;
}

} // namespace

class tst_Sales : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase_data() { TestSupport::addBackendRows(); }
    void cleanup() { m_core.close(); }

    // totals
    void lineDiscountsAndSaleDiscountAddUp();
    void vatByRate();
    void aSaleIsExactUntilItIsPaid();
    void onlyTheCashPartRounds();
    void scalePriceIsTheLineAmount();

    // the session
    void eachComputerIsARegister();
    void noSaleWithoutASession();
    void onlyOneSessionPerRegister();
    void sessionComesBackAfterARestart();
    void forgottenSessionStopsSales();
    void movementsNeedAReason();

    // selling
    void checkoutSavesEverything();
    void cashCheckoutRecordsTheRounding();
    void cardPaysTheExactAmount();
    void mixedPaymentRoundsOnlyTheCash();
    void coinlessRemainderRoundsAway();
    void electronicPaymentRules();
    void repeatedCodeIsFoundUnlessVoided();
    void sameArticleOnTwoLines();
    void shortPaymentIsRefused();
    void numbersAreConsecutivePerRegister();
    void failedSaleLeavesNothing();
    void stockMayGoNegative();
    void heldSaleSurvivesAndIsTakenOnce();
    void heldSalesBelongToTheRegister();

    // voiding
    void voidGivesBackStockAndCash();
    void voidNeedsAReasonAndHappensOnce();
    void voidOnlyInTheCurrentSession();

    // closing
    void matchingCountCloses();
    void wrongCountKeepsTheSessionOpen();
    void differenceNeedsAReasonAndStaysOnRecord();
    void closingMatchesTheSalesToTheGuarani();
    void closingCountsOnlyCashAndListsVouchers();
    void sessionsListTheHistory();

private:
    bool add(const QString &code, Quantity stock = AguaraQuantity::whole(10), Money price = 10000, Money cost = 7000);
    bool openWith(Money openingFloat = 100000);
    qint64 sell(const SaleDraft &draft, Money tendered = 10000000);
    qint64 sell(const QString &code, Money price = 10000);
    Quantity stock(const QString &code) const { return m_core.products->product(code)->stock; }
    Money expected();
    int rows(const QString &sql);

    QTemporaryDir m_tmp;
    CoreFixture m_core;
};

bool tst_Sales::add(const QString &code, Quantity stock, Money price, Money cost)
{
    Product p;
    p.code = code;
    p.description = QStringLiteral("ARTICULO ") + code;
    p.price = price;
    p.cost = cost;
    p.stock = stock;
    return m_core.products->saveProduct(p);
}

bool tst_Sales::openWith(Money openingFloat)
{
    return m_core.cash->openSession(worth(openingFloat));
}

qint64 tst_Sales::sell(const SaleDraft &draft, Money tendered)
{
    return m_core.sales->checkout(draft, { inCash(tendered) });
}

qint64 tst_Sales::sell(const QString &code, Money price)
{
    SaleDraft draft;
    draft.lines << lineOf(code, price);
    return sell(draft);
}

Money tst_Sales::expected()
{
    return m_core.cash->sessionReport(m_core.cash->currentSession()->id)->expectedCash;
}

int tst_Sales::rows(const QString &sql)
{
    QSqlQuery q(m_core.connection());
    if (!q.exec(sql) || !q.next())
        return -1;
    return q.value(0).toInt();
}

// --- totals ------------------------------------------------------------------

void tst_Sales::lineDiscountsAndSaleDiscountAddUp()
{
    SaleDraft draft;
    draft.lines << lineOf(QStringLiteral("A"), 10000) << lineOf(QStringLiteral("B"), 3333) << lineOf(QStringLiteral("C"), 3333);
    draft.lines[0].discount = 1000;
    draft.totalDiscount = 1000;

    const SaleTotals t = AguaraSale::totals(draft);
    QCOMPARE(t.gross, Money(16666));
    QCOMPARE(t.discount, Money(2000));
    QCOMPARE(t.net, Money(14666));

    Money discounts = 0, nets = 0;
    for (const SaleItem &item : t.items) {
        discounts += item.discount;
        nets += item.net;
        QCOMPARE(item.net, item.gross - item.discount);
    }
    QCOMPARE(discounts, t.discount);        // shares add up exactly
    QCOMPARE(nets, t.net);
    QVERIFY(t.items.at(0).discount > t.items.at(1).discount);   // in proportion
}

void tst_Sales::vatByRate()
{
    SaleDraft draft;
    draft.lines << lineOf(QStringLiteral("A"), 11000, AguaraQuantity::One, 10)
                << lineOf(QStringLiteral("B"), 5000, AguaraQuantity::One, 5)
                << lineOf(QStringLiteral("C"), 2000, AguaraQuantity::One, 0);
    const SaleTotals t = AguaraSale::totals(draft);
    QCOMPARE(t.taxed10, Money(11000));
    QCOMPARE(t.vat10, Money(1000));
    QCOMPARE(t.taxed5, Money(5000));
    QCOMPARE(t.vat5, Money(238));
    QCOMPARE(t.exempt, Money(2000));
    QCOMPARE(t.taxed10 + t.taxed5 + t.exempt, t.net);
}

void tst_Sales::aSaleIsExactUntilItIsPaid()
{
    SaleDraft draft;
    draft.lines << lineOf(QStringLiteral("A"), 4500, 1250);        // 1,250 kg: 5.625 Gs.
    const SaleTotals t = AguaraSale::totals(draft);
    QCOMPARE(t.net, Money(5625));
    QCOMPARE(t.total, Money(5625));         // a card or a transfer pays exactly this
    QCOMPARE(t.rounding, Money(0));
}

void tst_Sales::onlyTheCashPartRounds()
{
    QCOMPARE(AguaraSale::cashRounding(5625), Money(25));    // up
    QCOMPARE(AguaraSale::cashRounding(4959), Money(-9));    // down
    QCOMPARE(AguaraSale::cashRounding(4950), Money(0));
    QCOMPARE(AguaraSale::cashRounding(0), Money(0));        // nothing paid in cash
    QCOMPARE(AguaraSale::cashRounding(1), Money(-1));

    const Money due = 15625, byCard = 10000;                // mixed: the card pays exact amounts
    const Money cash = due - byCard;
    QCOMPARE(cash + AguaraSale::cashRounding(cash), Money(5650));
    QCOMPARE(AguaraSale::cashRounding(due - due), Money(0));    // all by card: exact
}

void tst_Sales::scalePriceIsTheLineAmount()
{
    SaleDraft draft;
    draft.lines << lineOf(QStringLiteral("A"), 48000, 719);        // 34.512 if it were multiplied
    draft.lines[0].fixedAmount = 34500;
    QCOMPARE(AguaraSale::totals(draft).total, Money(34500));
}

// --- the session -------------------------------------------------------------

void tst_Sales::eachComputerIsARegister()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    const CashRegister first = m_core.cash->currentRegister();
    QVERIFY(first.id != 0);
    QCOMPARE(first.name, QStringLiteral("Caja 1"));
    QCOMPARE(m_core.cash->currentRegister().id, first.id);      // found, not created again

    SqlCashService other(m_core.monitor.get(), QStringLiteral("PC-2"));
    const CashRegister second = other.currentRegister();
    QVERIFY(second.id != first.id);
    QCOMPARE(second.name, QStringLiteral("Caja 2"));

    QVERIFY(m_core.cash->renameRegister(QStringLiteral("Caja mostrador")));
    QCOMPARE(m_core.cash->currentRegister().name, QStringLiteral("Caja mostrador"));
    QVERIFY(!m_core.cash->renameRegister(QStringLiteral("  ")));
}

void tst_Sales::noSaleWithoutASession()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("A")));
    QVERIFY(!m_core.cash->currentSession());
    QCOMPARE(sell(QStringLiteral("A")), qint64(0));
    QVERIFY(m_core.sales->lastError().contains(QStringLiteral("cerrada")));
    QCOMPARE(stock(QStringLiteral("A")), AguaraQuantity::whole(10));
}

void tst_Sales::onlyOneSessionPerRegister()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(m_core.cash->openSession(notes(50000, 2)));
    QVERIFY(!m_core.cash->openSession(notes(50000, 2)));

    const auto session = m_core.cash->currentSession();
    QVERIFY(session.has_value());
    QCOMPARE(session->openingFloat, Money(100000));
    const auto report = m_core.cash->sessionReport(session->id);
    QCOMPARE(report->openingCount.size(), 1);
    QCOMPARE(report->openingCount.first().denomination, Money(50000));
    QCOMPARE(report->openingCount.first().count, 2);

    SqlCashService other(m_core.monitor.get(), QStringLiteral("PC-2"));     // another till isn't affected
    QVERIFY(other.openSession(notes(50000, 1)));
}

void tst_Sales::sessionComesBackAfterARestart()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("A")));
    QVERIFY(openWith(100000));
    QVERIFY(sell(QStringLiteral("A")));
    const qint64 id = m_core.cash->currentSession()->id;

    SqlCashService restarted(m_core.monitor.get(), QStringLiteral("PC-TEST"));
    const auto session = restarted.currentSession();
    QVERIFY(session.has_value());
    QCOMPARE(session->id, id);
    QVERIFY(!session->isStale());
    QCOMPARE(restarted.sessionReport(id)->expectedCash, Money(110000));
}

void tst_Sales::forgottenSessionStopsSales()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("A")));
    QVERIFY(openWith(100000));
    const QString yesterday = QDateTime::currentDateTime().addDays(-1).toString(Qt::ISODate);
    QSqlQuery q(m_core.connection());
    QVERIFY(q.exec(QStringLiteral("UPDATE sesiones_caja SET apertura = '%1'").arg(yesterday)));

    const auto session = m_core.cash->currentSession();
    QVERIFY(session->isStale());
    QCOMPARE(sell(QStringLiteral("A")), qint64(0));
    QVERIFY(m_core.sales->lastError().contains(QStringLiteral("otro día")));
    QCOMPARE(m_core.cash->closeSession(worth(100000)), CloseResult::Closed);
    QVERIFY(openWith(50000));
    QVERIFY(sell(QStringLiteral("A")));
}

void tst_Sales::movementsNeedAReason()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(!m_core.cash->addMovement(CashMovementType::In, 1000, QStringLiteral("sin caja abierta")));
    QVERIFY(openWith(100000));
    QVERIFY(!m_core.cash->addMovement(CashMovementType::Out, 1000, QStringLiteral("  ")));
    QVERIFY(!m_core.cash->addMovement(CashMovementType::Out, 0, QStringLiteral("cero")));
    QVERIFY(!m_core.cash->addMovement("otro", 1000, QStringLiteral("tipo")));

    QVERIFY(m_core.cash->addMovement(CashMovementType::In, 20000, QStringLiteral("Cambio")));
    QVERIFY(m_core.cash->addMovement(CashMovementType::Out, 15000, QStringLiteral("Pago de hielo")));
    QVERIFY(m_core.cash->addMovement(CashMovementType::Withdrawal, 50000, QStringLiteral("Al cofre")));
    QCOMPARE(expected(), Money(55000));

    const auto list = m_core.cash->movements(m_core.cash->currentSession()->id);
    QCOMPARE(list.size(), 3);
    QCOMPARE(list.at(2).type, QStringLiteral("retiro"));
    QCOMPARE(list.at(2).amount, Money(50000));
    QCOMPARE(list.at(1).reason, QStringLiteral("Pago de hielo"));
}

// --- selling -----------------------------------------------------------------

void tst_Sales::checkoutSavesEverything()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("A"), AguaraQuantity::whole(10), 11000, 7000));
    QVERIFY(add(QStringLiteral("B"), 5000, 5000, 3000));
    QVERIFY(openWith(100000));

    SaleDraft draft;
    draft.lines << lineOf(QStringLiteral("A"), 11000, AguaraQuantity::whole(2), 10)
                << lineOf(QStringLiteral("B"), 5000, 1500, 5);
    const qint64 id = sell(draft, 50000);
    QVERIFY2(id, qPrintable(m_core.sales->lastError()));

    const auto sale = m_core.sales->sale(id);
    QVERIFY(sale.has_value());
    QCOMPARE(sale->number, qint64(1));
    QCOMPARE(sale->state, QStringLiteral("cobrada"));
    QCOMPARE(sale->totals.total, Money(29500));
    QCOMPARE(sale->totals.vat10, Money(2000));
    QCOMPARE(sale->totals.items.size(), 2);
    QCOMPARE(sale->totals.items.at(1).quantity, Quantity(1500));
    QCOMPARE(sale->totals.items.at(1).net, Money(7500));
    QCOMPARE(sale->payments.size(), 1);
    QCOMPARE(sale->payments.first().method, QStringLiteral("efectivo"));
    QCOMPARE(sale->payments.first().amount, Money(50000));
    QCOMPARE(sale->payments.first().change, Money(20500));

    QCOMPARE(stock(QStringLiteral("A")), AguaraQuantity::whole(8));
    QCOMPARE(stock(QStringLiteral("B")), Quantity(3500));
    const auto kardex = m_core.inventory->movements(QStringLiteral("A"));
    QCOMPARE(kardex.first().type, QStringLiteral("venta"));
    QCOMPARE(kardex.first().quantity, -AguaraQuantity::whole(2));
    QCOMPARE(kardex.first().document, QStringLiteral("Venta 001-000001"));
    QCOMPARE(expected(), Money(129500));        // float + the sale; change went back out
}

void tst_Sales::cashCheckoutRecordsTheRounding()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("A"), AguaraQuantity::whole(10), 4500));
    QVERIFY(openWith(100000));
    SaleDraft draft;
    draft.lines << lineOf(QStringLiteral("A"), 4500, 1250);

    QCOMPARE(sell(draft, 5625), qint64(0));                 // cash can't pay 5.625
    QVERIFY(m_core.sales->lastError().contains(QStringLiteral("5.650")));
    const qint64 id = sell(draft, 10000);
    QVERIFY(id);
    const auto sale = m_core.sales->sale(id);
    QCOMPARE(sale->totals.net, Money(5625));
    QCOMPARE(sale->totals.rounding, Money(25));
    QCOMPARE(sale->totals.total, Money(5650));
    QCOMPARE(sale->payments.first().change, Money(4350));
    QCOMPARE(expected(), Money(105650));

    const auto report = m_core.cash->sessionReport(sale->sessionId);
    QCOMPARE(report->salesTotal, Money(5650));
    QCOMPARE(report->rounding, Money(25));
}

void tst_Sales::cardPaysTheExactAmount()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("A"), AguaraQuantity::whole(10), 4500));
    QVERIFY(openWith(100000));
    SaleDraft draft;
    draft.lines << lineOf(QStringLiteral("A"), 4500, 1250);     // 5.625

    const qint64 id = m_core.sales->checkout(draft, { byVoucher(PaymentMethod::Debit, 5625, QStringLiteral(" ab123 ")) });
    QVERIFY2(id, qPrintable(m_core.sales->lastError()));
    const auto sale = m_core.sales->sale(id);
    QCOMPARE(sale->totals.rounding, Money(0));
    QCOMPARE(sale->totals.total, Money(5625));
    QCOMPARE(sale->payments.size(), 1);
    QCOMPARE(sale->payments.first().method, QStringLiteral("tarjeta_debito"));
    QCOMPARE(sale->payments.first().amount, Money(5625));
    QCOMPARE(sale->payments.first().change, Money(0));
    QCOMPARE(sale->payments.first().reference, QStringLiteral("AB123"));
    QCOMPARE(expected(), Money(100000));                        // nothing entered the drawer
    QCOMPARE(stock(QStringLiteral("A")), AguaraQuantity::whole(10) - 1250);
}

void tst_Sales::mixedPaymentRoundsOnlyTheCash()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("A"), AguaraQuantity::whole(10), 12500));
    QVERIFY(openWith(100000));
    SaleDraft draft;
    draft.lines << lineOf(QStringLiteral("A"), 12500, 1250);    // 15.625

    const qint64 id = m_core.sales->checkout(draft, {
        byVoucher(PaymentMethod::Credit, 6000, QStringLiteral("111")),
        byVoucher(PaymentMethod::Transfer, 4000, QStringLiteral("222")),
        inCash(6000) });
    QVERIFY2(id, qPrintable(m_core.sales->lastError()));
    const auto sale = m_core.sales->sale(id);
    QCOMPARE(sale->totals.net, Money(15625));
    QCOMPARE(sale->totals.rounding, Money(25));                 // 5.625 rounds to 5.650
    QCOMPARE(sale->totals.total, Money(15650));
    QCOMPARE(sale->payments.size(), 3);
    QCOMPARE(sale->payments.at(2).method, QStringLiteral("efectivo"));
    QCOMPARE(sale->payments.at(2).change, Money(350));
    QVERIFY(sale->payments.at(2).reference.isEmpty());
    QCOMPARE(expected(), Money(105650));
}

/// 20 Gs. left and no coin to pay them: the card's voucher is already printed,
/// so the sale must not get stuck.
void tst_Sales::coinlessRemainderRoundsAway()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("A"), AguaraQuantity::whole(10), 4500));
    QVERIFY(openWith(100000));
    SaleDraft draft;
    draft.lines << lineOf(QStringLiteral("A"), 4500, 1250);     // 5.625

    const qint64 id = m_core.sales->checkout(draft, { byVoucher(PaymentMethod::Debit, 5605, QStringLiteral("1")) });
    QVERIFY2(id, qPrintable(m_core.sales->lastError()));
    const auto sale = m_core.sales->sale(id);
    QCOMPARE(sale->totals.rounding, Money(-20));
    QCOMPARE(sale->totals.total, Money(5605));
    QCOMPARE(sale->payments.size(), 1);
    QCOMPARE(expected(), Money(100000));
}

void tst_Sales::electronicPaymentRules()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("A")));
    QVERIFY(openWith(100000));
    SaleDraft draft;
    draft.lines << lineOf(QStringLiteral("A"), 10000);

    const auto refused = [&](const QList<SalePayment> &payments, const QString &because) {
        const bool stopped = m_core.sales->checkout(draft, payments) == 0;
        if (!stopped || !m_core.sales->lastError().contains(because))
            qWarning() << "got:" << m_core.sales->lastError();
        return stopped && m_core.sales->lastError().contains(because);
    };
    const QString code = QStringLiteral("9");
    QVERIFY(refused({ byVoucher(PaymentMethod::Debit, 10000, QStringLiteral("  ")) }, QStringLiteral("código")));
    QVERIFY(refused({ byVoucher(PaymentMethod::Debit, 10001, code) }, QStringLiteral("vuelto")));
    QVERIFY(refused({ byVoucher(PaymentMethod::Debit, 0, code) }, QStringLiteral("montos")));
    QVERIFY(refused({ byVoucher("bitcoin", 10000, code) }, QStringLiteral("no existe")));
    QVERIFY(refused({ byVoucher(PaymentMethod::Debit, 5000, QStringLiteral("x")),
                      byVoucher(PaymentMethod::Credit, 5000, QStringLiteral(" X ")) }, QStringLiteral("dos veces")));
    QVERIFY(refused({ byVoucher(PaymentMethod::Debit, 5000, code), inCash(4900) }, QStringLiteral("5.000")));
    QVERIFY(refused({ byVoucher(PaymentMethod::Debit, 10000, code), inCash(1000) }, QStringLiteral("nada")));
    QVERIFY(refused({ inCash(5000), inCash(5000) }, QStringLiteral("un solo pago")));
    QVERIFY(refused({}, QStringLiteral("Falta dinero")));

    QCOMPARE(rows(QStringLiteral("SELECT COUNT(*) FROM ventas")), 0);
    QCOMPARE(stock(QStringLiteral("A")), AguaraQuantity::whole(10));
}

void tst_Sales::repeatedCodeIsFoundUnlessVoided()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("A")));
    QVERIFY(openWith(100000));
    SaleDraft draft;
    draft.lines << lineOf(QStringLiteral("A"), 10000);

    QVERIFY(m_core.sales->referenceUsedBy(QStringLiteral("abc")).isEmpty());
    const qint64 id = m_core.sales->checkout(draft, { byVoucher(PaymentMethod::Debit, 10000, QStringLiteral("abc")) });
    QVERIFY(id);
    QCOMPARE(m_core.sales->referenceUsedBy(QStringLiteral(" ABC ")), QStringLiteral("001-000001"));
    QVERIFY(m_core.sales->voidSale(id, QStringLiteral("Mal cobrado")));
    QVERIFY(m_core.sales->referenceUsedBy(QStringLiteral("abc")).isEmpty());
}

void tst_Sales::sameArticleOnTwoLines()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("A")));
    QVERIFY(openWith());
    SaleDraft draft;
    draft.lines << lineOf(QStringLiteral("A"), 10000) << lineOf(QStringLiteral("A"), 10000);
    QVERIFY(sell(draft));
    QCOMPARE(stock(QStringLiteral("A")), AguaraQuantity::whole(8));
}

void tst_Sales::shortPaymentIsRefused()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("A")));
    QVERIFY(openWith());
    SaleDraft draft;
    draft.lines << lineOf(QStringLiteral("A"), 10000);
    QCOMPARE(sell(draft, 9999), qint64(0));
    QVERIFY(m_core.sales->lastError().contains(QStringLiteral("10.000")));
    QVERIFY(sell(draft, 10000));

    SaleDraft empty;
    QCOMPARE(sell(empty), qint64(0));
}

void tst_Sales::numbersAreConsecutivePerRegister()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("A"), AguaraQuantity::whole(50)));
    QVERIFY(openWith());
    SqlCashService otherCash(m_core.monitor.get(), QStringLiteral("PC-2"));
    SqlSalesService otherSales(m_core.monitor.get(), m_core.products.get(), &otherCash);
    QVERIFY(otherCash.openSession(worth(1000)));

    SaleDraft draft;
    draft.lines << lineOf(QStringLiteral("A"), 10000);
    const qint64 a1 = sell(draft);
    const qint64 b1 = otherSales.checkout(draft, { inCash(20000) });
    const qint64 a2 = sell(draft);
    QVERIFY(a1 && b1 && a2);
    QCOMPARE(m_core.sales->sale(a1)->number, qint64(1));
    QCOMPARE(m_core.sales->sale(b1)->number, qint64(1));        // the other register has its own
    QCOMPARE(m_core.sales->sale(a2)->number, qint64(2));
    QVERIFY(m_core.sales->sale(a1)->registerId != m_core.sales->sale(b1)->registerId);
    QCOMPARE(AguaraSale::formatNumber(1, 123), QStringLiteral("001-000123"));
}

void tst_Sales::failedSaleLeavesNothing()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("A")));
    QVERIFY(openWith());
    const auto session = m_core.cash->currentSession();

    // number 1 is taken, so the sale fails inside its transaction
    QSqlQuery q(m_core.connection());
    QVERIFY(q.exec(QStringLiteral(
        "INSERT INTO ventas (caja_id, sesion_id, numero, estado, bruto, descuento, neto, redondeo, total,"
        " gravado10, iva10, gravado5, iva5, exento) VALUES (%1, %2, 1, 'cobrada', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)")
        .arg(session->registerId).arg(session->id)));

    QCOMPARE(sell(QStringLiteral("A")), qint64(0));
    QCOMPARE(stock(QStringLiteral("A")), AguaraQuantity::whole(10));
    QCOMPARE(rows(QStringLiteral("SELECT COUNT(*) FROM ventas")), 1);
    QCOMPARE(rows(QStringLiteral("SELECT COUNT(*) FROM ventas_pagos")), 0);
    QCOMPARE(rows(QStringLiteral("SELECT ultimo_numero FROM cajas")), 0);       // the number came back
    QCOMPARE(rows(QStringLiteral("SELECT COUNT(*) FROM stock_movimientos WHERE tipo = 'venta'")), 0);
}

void tst_Sales::stockMayGoNegative()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("A"), AguaraQuantity::One));
    QVERIFY(openWith());
    SaleDraft draft;
    draft.lines << lineOf(QStringLiteral("A"), 10000, AguaraQuantity::whole(3));
    QVERIFY(sell(draft));
    QCOMPARE(stock(QStringLiteral("A")), -AguaraQuantity::whole(2));
}

void tst_Sales::heldSaleSurvivesAndIsTakenOnce()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("A")));
    QVERIFY(openWith());
    SaleDraft draft;
    draft.lines << lineOf(QStringLiteral("A"), 10000, 2500);
    draft.lines[0].discount = 1000;
    draft.lines[0].fixedAmount = 24000;
    draft.lines[0].unit = QStringLiteral("kg");
    draft.lines[0].decimals = 3;
    draft.totalDiscount = 500;
    const qint64 id = m_core.sales->holdSale(draft);
    QVERIFY(id);
    QCOMPARE(stock(QStringLiteral("A")), AguaraQuantity::whole(10));    // nothing sold yet

    SqlCashService restartedCash(m_core.monitor.get(), QStringLiteral("PC-TEST"));
    SqlSalesService restarted(m_core.monitor.get(), m_core.products.get(), &restartedCash);
    const QList<HeldSale> held = restarted.heldSales();
    QCOMPARE(held.size(), 1);
    QCOMPARE(held.first().draft.totalDiscount, Money(500));

    const auto taken = restarted.takeHeldSale(id);
    QVERIFY(taken.has_value());
    QCOMPARE(taken->lines.size(), 1);
    const SaleLine &line = taken->lines.first();
    QCOMPARE(line.code, QStringLiteral("A"));
    QCOMPARE(line.quantity, Quantity(2500));
    QCOMPARE(line.fixedAmount, Money(24000));
    QCOMPARE(line.discount, Money(1000));
    QCOMPARE(line.unit, QStringLiteral("kg"));
    QCOMPARE(line.decimals, 3);
    QVERIFY(!restarted.takeHeldSale(id).has_value());
    QVERIFY(restarted.heldSales().isEmpty());
}

void tst_Sales::heldSalesBelongToTheRegister()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("A")));
    SaleDraft draft;
    draft.lines << lineOf(QStringLiteral("A"), 10000);
    const qint64 id = m_core.sales->holdSale(draft);
    QVERIFY(id);
    SqlCashService otherCash(m_core.monitor.get(), QStringLiteral("PC-2"));
    SqlSalesService otherSales(m_core.monitor.get(), m_core.products.get(), &otherCash);
    QVERIFY(otherSales.heldSales().isEmpty());
    QVERIFY(!otherSales.takeHeldSale(id).has_value());
}

// --- voiding -----------------------------------------------------------------

void tst_Sales::voidGivesBackStockAndCash()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("A")));
    QVERIFY(openWith(100000));
    SaleDraft draft;
    draft.lines << lineOf(QStringLiteral("A"), 10000, AguaraQuantity::whole(3));
    const qint64 id = sell(draft, 50000);
    QVERIFY(id);
    QCOMPARE(expected(), Money(130000));

    QVERIFY2(m_core.sales->voidSale(id, QStringLiteral("Cliente se arrepintió")), qPrintable(m_core.sales->lastError()));
    QCOMPARE(stock(QStringLiteral("A")), AguaraQuantity::whole(10));
    QCOMPARE(expected(), Money(100000));

    const auto sale = m_core.sales->sale(id);
    QCOMPARE(sale->state, QStringLiteral("anulada"));
    QCOMPARE(sale->voidReason, QStringLiteral("Cliente se arrepintió"));
    QVERIFY(sale->voidedAt.isValid());
    const auto kardex = m_core.inventory->movements(QStringLiteral("A"));
    QCOMPARE(kardex.first().type, QStringLiteral("devolucion"));
    QCOMPARE(kardex.first().quantity, AguaraQuantity::whole(3));

    const auto report = m_core.cash->sessionReport(sale->sessionId);
    QCOMPARE(report->salesCount, 0);
    QCOMPARE(report->salesTotal, Money(0));
    QCOMPARE(report->voidedCount, 1);
    QCOMPARE(report->voidedTotal, Money(30000));
    QCOMPARE(m_core.cash->closeSession(worth(100000)), CloseResult::Closed);
}

void tst_Sales::voidNeedsAReasonAndHappensOnce()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("A")));
    QVERIFY(openWith());
    const qint64 id = sell(QStringLiteral("A"));
    QVERIFY(id);
    QVERIFY(!m_core.sales->voidSale(id, QStringLiteral("  ")));
    QVERIFY(m_core.sales->voidSale(id, QStringLiteral("Error")));
    QVERIFY(!m_core.sales->voidSale(id, QStringLiteral("Otra vez")));
    QCOMPARE(stock(QStringLiteral("A")), AguaraQuantity::whole(10));    // given back once
    QVERIFY(!m_core.sales->voidSale(9999, QStringLiteral("No existe")));
}

void tst_Sales::voidOnlyInTheCurrentSession()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("A")));
    QVERIFY(openWith(100000));
    const qint64 id = sell(QStringLiteral("A"));
    QCOMPARE(m_core.cash->closeSession(worth(110000)), CloseResult::Closed);
    QVERIFY(!m_core.sales->voidSale(id, QStringLiteral("Ya cerrada")));     // no session
    QVERIFY(openWith(100000));
    QVERIFY(!m_core.sales->voidSale(id, QStringLiteral("Otra sesión")));
    QVERIFY(m_core.sales->lastError().contains(QStringLiteral("sesión")));
    QCOMPARE(m_core.sales->sale(id)->state, QStringLiteral("cobrada"));
}

// --- closing -----------------------------------------------------------------

void tst_Sales::matchingCountCloses()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("A")));
    QVERIFY(openWith(100000));
    QVERIFY(sell(QStringLiteral("A")));
    const qint64 id = m_core.cash->currentSession()->id;

    CashCount count = notes(100000, 1);
    count += notes(10000, 1);
    QCOMPARE(m_core.cash->closeSession(count), CloseResult::Closed);
    QVERIFY(!m_core.cash->currentSession());

    const auto report = m_core.cash->sessionReport(id);
    QVERIFY(!report->session.open);
    QCOMPARE(report->session.expected, Money(110000));
    QCOMPARE(report->session.counted, Money(110000));
    QCOMPARE(report->session.difference, Money(0));
    QCOMPARE(report->closingCount.size(), 2);
    QCOMPARE(m_core.cash->closeSession(count), CloseResult::Failed);        // nothing left to close
}

void tst_Sales::wrongCountKeepsTheSessionOpen()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(openWith(100000));
    QCOMPARE(m_core.cash->closeSession(worth(99950)), CloseResult::Mismatch);
    QVERIFY(m_core.cash->currentSession().has_value());
    QCOMPARE(rows(QStringLiteral("SELECT COUNT(*) FROM sesion_conteos WHERE momento = 'cierre'")), 0);
    QCOMPARE(m_core.cash->closeSession(worth(100000)), CloseResult::Closed);    // counted again
}

void tst_Sales::differenceNeedsAReasonAndStaysOnRecord()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(openWith(100000));
    const qint64 id = m_core.cash->currentSession()->id;
    QVERIFY(!m_core.cash->closeWithDifference(worth(90000), QStringLiteral(" "), QStringLiteral("encargado")));
    QVERIFY(m_core.cash->currentSession().has_value());

    QVERIFY(m_core.cash->closeWithDifference(worth(90000), QStringLiteral("Vuelto mal dado"), QStringLiteral("encargado")));
    const auto session = m_core.cash->sessionReport(id)->session;
    QCOMPARE(session.expected, Money(100000));
    QCOMPARE(session.counted, Money(90000));
    QCOMPARE(session.difference, Money(-10000));
    QCOMPARE(session.differenceReason, QStringLiteral("Vuelto mal dado"));
    QCOMPARE(session.authorizedBy, QStringLiteral("encargado"));
}

/// The phase's promise: the closing is what the sales say, to the guaraní.
void tst_Sales::closingMatchesTheSalesToTheGuarani()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("A"), AguaraQuantity::whole(100), 12350));
    QVERIFY(add(QStringLiteral("B"), AguaraQuantity::whole(100), 4500));
    QVERIFY(openWith(150000));
    const qint64 sessionId = m_core.cash->currentSession()->id;

    Money sum = 0;
    qint64 toVoid = 0;
    for (int i = 1; i <= 12; ++i) {
        SaleDraft draft;
        draft.lines << lineOf(QStringLiteral("A"), 12350, AguaraQuantity::whole(i), 10)
                    << lineOf(QStringLiteral("B"), 4500, 1000 + 137 * i, 5);
        draft.lines[0].discount = i % 3 == 0 ? 777 : 0;
        draft.totalDiscount = i % 4 == 0 ? 1234 : 0;
        const qint64 id = sell(draft);
        QVERIFY2(id, qPrintable(m_core.sales->lastError()));
        if (i == 5)
            toVoid = id;
        else
            sum += net(draft) + AguaraSale::cashRounding(net(draft));
    }
    QVERIFY(m_core.sales->voidSale(toVoid, QStringLiteral("Error de carga")));
    QVERIFY(m_core.cash->addMovement(CashMovementType::Withdrawal, 200000, QStringLiteral("Al cofre")));

    const auto report = m_core.cash->sessionReport(sessionId);
    QCOMPARE(report->salesTotal, sum);
    QCOMPARE(report->salesCount, 11);
    QCOMPARE(report->payments.size(), 1);
    QCOMPARE(report->payments.first().amount, sum);
    QCOMPARE(report->expectedCash, 150000 + sum - 200000);
    QCOMPARE(report->taxed10 + report->taxed5 + report->exempt + report->rounding, sum);

    // the lines say the same as the heads
    QCOMPARE(qint64(rows(QStringLiteral(
        "SELECT CAST(SUM(i.importe) AS BIGINT) FROM ventas_items i JOIN ventas v ON v.id = i.venta_id"
        " WHERE v.estado = 'cobrada'"))) + report->rounding, sum);

    QCOMPARE(m_core.cash->closeSession(worth(report->expectedCash)), CloseResult::Closed);
    QCOMPARE(m_core.cash->sessionReport(sessionId)->session.counted, 150000 + sum - 200000);
}

void tst_Sales::closingCountsOnlyCashAndListsVouchers()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("A")));
    QVERIFY(openWith(100000));
    SaleDraft draft;
    draft.lines << lineOf(QStringLiteral("A"), 10000);

    QVERIFY(m_core.sales->checkout(draft, { byVoucher(PaymentMethod::Credit, 7000, QStringLiteral("C1")), inCash(3000) }));
    QVERIFY(m_core.sales->checkout(draft, { byVoucher(PaymentMethod::Transfer, 10000, QStringLiteral("T1")) }));
    const qint64 voided = m_core.sales->checkout(draft, { byVoucher(PaymentMethod::Debit, 10000, QStringLiteral("D1")) });
    QVERIFY(voided);
    QVERIFY(m_core.sales->voidSale(voided, QStringLiteral("Error")));

    const qint64 sessionId = m_core.cash->currentSession()->id;
    QCOMPARE(expected(), Money(103000));
    QCOMPARE(m_core.cash->closeSession(worth(103000)), CloseResult::Closed);

    const auto report = m_core.cash->sessionReport(sessionId);
    QCOMPARE(report->salesTotal, Money(20000));
    QCOMPARE(report->payments.size(), 3);
    Money sum = 0;
    for (const PaymentTotal &p : report->payments)
        sum += p.amount;
    QCOMPARE(sum, report->salesTotal);
    QCOMPARE(report->vouchers.size(), 2);                       // not the voided one
    QCOMPARE(report->vouchers.at(0).reference, QStringLiteral("C1"));
    QCOMPARE(report->vouchers.at(0).amount, Money(7000));
    QCOMPARE(report->vouchers.at(0).saleNumber, QStringLiteral("001-000001"));
    QCOMPARE(report->vouchers.at(1).method, QStringLiteral("transferencia"));
}

void tst_Sales::sessionsListTheHistory()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(openWith(1000));
    QCOMPARE(m_core.cash->closeSession(worth(1000)), CloseResult::Closed);
    QVERIFY(openWith(2000));
    const QList<CashSession> list = m_core.cash->sessions();
    QCOMPARE(list.size(), 2);
    QVERIFY(list.first().open);
    QCOMPARE(list.first().openingFloat, Money(2000));
    QCOMPARE(list.last().registerName, QStringLiteral("Caja 1"));
    QVERIFY(list.last().closed.isValid());
}

QTEST_GUILESS_MAIN(tst_Sales)
#include "tst_sales.moc"
