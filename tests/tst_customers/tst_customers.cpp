// Customers: the RUC's check digit, the rules of the list, and the customer
// of a sale, also held or in the queue.
#include "testsupport.h"

#include <QTemporaryDir>
#include <QtTest>

using TestSupport::CoreFixture;
using TestSupport::inCash;

class tst_Customers : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase_data() { TestSupport::addBackendRows(); }

    void checkDigit_data();
    void checkDigit();
    void rucNeedsItsRightDigit();
    void ciIsDigitsOnly();

    void saveAndFind();
    void documentIsNotRepeated();
    void manyWithoutADocument();
    void leavingKeepsTheCustomer();
    void nameIsRequired();

    void saleKeepsItsCustomer();
    void saleWithoutCustomerIsTheFinalConsumer();
    void heldSaleKeepsItsCustomer();
    void queuedSaleKeepsItsCustomer();
    void ticketNamesTheCustomer();

private:
    Customer ande() const;
    SaleDraft draftFor(const Customer &customer) const;
    bool prepare();

    QTemporaryDir m_tmp;
    CoreFixture m_core;
};

Customer tst_Customers::ande() const
{
    Customer c;
    c.name = QStringLiteral("ANDE");
    c.documentType = QString::fromLatin1(DocumentType::Ruc);
    c.document = QStringLiteral("80009735-1");
    c.phone = QStringLiteral("021 555");
    return c;
}

SaleDraft tst_Customers::draftFor(const Customer &customer) const
{
    SaleLine line;
    line.code = QStringLiteral("A");
    line.description = QStringLiteral("ARTICULO");
    line.unitPrice = 10000;
    SaleDraft draft;
    draft.lines << line;
    draft.customerId = customer.id;
    draft.customerName = customer.name;
    draft.customerDocument = customer.document;
    return draft;
}

bool tst_Customers::prepare()
{
    Product p;
    p.code = QStringLiteral("A");
    p.description = QStringLiteral("ARTICULO");
    p.price = 10000;
    p.stock = AguaraQuantity::whole(100);
    DenominationCount note;
    note.denomination = 100000;
    note.count = 1;
    return m_core.products->saveProduct(p) && m_core.cash->openSession({ note });
}

void tst_Customers::checkDigit_data()
{
    QTest::addColumn<QString>("base");
    QTest::addColumn<int>("digit");
    // public RUCs: worked out by hand, and printed on their own invoices
    QTest::newRow("ANDE") << "80009735" << 1;
    QTest::newRow("COPACO") << "80000519" << 8;
    QTest::newRow("remainder 1 gives 0") << "6" << 0;       // 6*2 = 12, remainder 1
    QTest::newRow("short") << "1" << 9;                     // 1*2 = 2: 11 - 2
    QTest::newRow("a person") << "1234567" << AguaraCustomer::rucCheckDigit(QStringLiteral("1234567"));
}

void tst_Customers::checkDigit()
{
    QFETCH(QString, base);
    QFETCH(int, digit);
    QCOMPARE(AguaraCustomer::rucCheckDigit(base), digit);
}

void tst_Customers::rucNeedsItsRightDigit()
{
    QVERIFY(AguaraCustomer::validRuc(QStringLiteral("80009735-1")));
    QVERIFY(AguaraCustomer::validRuc(QStringLiteral("80000519-8")));
    QVERIFY(!AguaraCustomer::validRuc(QStringLiteral("80009735-2")));
    QVERIFY(!AguaraCustomer::validRuc(QStringLiteral("80009735")));
    QVERIFY(!AguaraCustomer::validRuc(QStringLiteral("-1")));
    QVERIFY(!AguaraCustomer::validRuc(QString()));
    // two digits swapped: what a check digit exists to catch
    QVERIFY(!AguaraCustomer::validRuc(QStringLiteral("80009753-1")));
}

void tst_Customers::ciIsDigitsOnly()
{
    QVERIFY(AguaraCustomer::validCi(QStringLiteral("1234567")));
    QVERIFY(!AguaraCustomer::validCi(QStringLiteral("1.234.567")));
    QVERIFY(!AguaraCustomer::validCi(QString()));
}

void tst_Customers::saveAndFind()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    Customer c = ande();
    c.id = m_core.customers->saveCustomer(c);
    QVERIFY2(c.id, qPrintable(m_core.customers->lastError()));

    QCOMPARE(m_core.customers->customers(QStringLiteral("and")).size(), 1);
    QCOMPARE(m_core.customers->customers(QStringLiteral("80009")).size(), 1);
    QCOMPARE(m_core.customers->customers(QStringLiteral("otro")).size(), 0);
    QCOMPARE(m_core.customers->customer(c.id)->phone, QStringLiteral("021 555"));

    c.name = QStringLiteral("  ANDE   CENTRAL ");
    QCOMPARE(m_core.customers->saveCustomer(c), c.id);
    QCOMPARE(m_core.customers->customer(c.id)->name, QStringLiteral("ANDE CENTRAL"));
}

void tst_Customers::documentIsNotRepeated()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(m_core.customers->saveCustomer(ande()));
    Customer other = ande();
    other.name = QStringLiteral("OTRA");
    QCOMPARE(m_core.customers->saveCustomer(other), qint64(0));
    QVERIFY(m_core.customers->lastError().contains(QStringLiteral("ANDE")));
}

void tst_Customers::manyWithoutADocument()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    Customer c;
    c.name = QStringLiteral("DON JUAN");
    QVERIFY(m_core.customers->saveCustomer(c));
    c.name = QStringLiteral("DOÑA ROSA");
    QVERIFY2(m_core.customers->saveCustomer(c), qPrintable(m_core.customers->lastError()));
    Customer bad = ande();
    bad.document = QStringLiteral("80009735-2");
    QCOMPARE(m_core.customers->saveCustomer(bad), qint64(0));
    QVERIFY(m_core.customers->lastError().contains(QStringLiteral("RUC")));
}

void tst_Customers::leavingKeepsTheCustomer()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    const qint64 id = m_core.customers->saveCustomer(ande());
    QVERIFY(m_core.customers->setCustomerActive(id, false));
    QCOMPARE(m_core.customers->customers().size(), 0);
    QCOMPARE(m_core.customers->customers(QString(), true).size(), 1);
    QVERIFY(!m_core.customers->customer(id)->active);
    QVERIFY(m_core.customers->setCustomerActive(id, true));
    QCOMPARE(m_core.customers->customers().size(), 1);
}

void tst_Customers::nameIsRequired()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    Customer c;
    c.name = QStringLiteral("   ");
    QCOMPARE(m_core.customers->saveCustomer(c), qint64(0));
}

void tst_Customers::saleKeepsItsCustomer()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(prepare());
    Customer c = ande();
    c.id = m_core.customers->saveCustomer(c);
    const qint64 id = m_core.sales->checkout(draftFor(c), { inCash(10000) });
    QVERIFY2(id > 0, qPrintable(m_core.sales->lastError()));

    // renaming later doesn't rewrite the sale
    c.name = QStringLiteral("OTRO NOMBRE");
    QVERIFY(m_core.customers->saveCustomer(c));
    const auto sale = m_core.sales->sale(id);
    QCOMPARE(sale->customerId, c.id);
    QCOMPARE(sale->customerName, QStringLiteral("ANDE"));
    QCOMPARE(sale->customerDocument, QStringLiteral("80009735-1"));
}

void tst_Customers::saleWithoutCustomerIsTheFinalConsumer()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(prepare());
    const qint64 id = m_core.sales->checkout(draftFor(Customer()), { inCash(10000) });
    QVERIFY2(id > 0, qPrintable(m_core.sales->lastError()));
    QCOMPARE(m_core.sales->sale(id)->customerId, qint64(0));
    QVERIFY(m_core.sales->sale(id)->customerName.isEmpty());
}

void tst_Customers::heldSaleKeepsItsCustomer()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(prepare());
    Customer c = ande();
    c.id = m_core.customers->saveCustomer(c);
    const qint64 held = m_core.sales->holdSale(draftFor(c));
    QVERIFY(held);
    const auto draft = m_core.sales->takeHeldSale(held);
    QCOMPARE(draft->customerId, c.id);
    QCOMPARE(draft->customerName, QStringLiteral("ANDE"));
}

void tst_Customers::queuedSaleKeepsItsCustomer()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(prepare());
    Customer c = ande();
    c.id = m_core.customers->saveCustomer(c);
    m_core.monitor->simulateOutage(true);
    const qint64 id = m_core.sales->checkout(draftFor(c), { inCash(10000) });
    QVERIFY(id < 0);
    QCOMPARE(m_core.sales->sale(id)->customerName, QStringLiteral("ANDE"));
    m_core.monitor->simulateOutage(false);
    QVERIFY(TestSupport::waitFor([&] { return m_core.sales->pendingSales().isEmpty(); }));
    const auto list = m_core.sales->sessionSales(m_core.cash->currentSession()->id);
    QCOMPARE(list.first().customerId, c.id);
}

void tst_Customers::ticketNamesTheCustomer()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(prepare());
    Customer c = ande();
    c.id = m_core.customers->saveCustomer(c);
    const qint64 id = m_core.sales->checkout(draftFor(c), { inCash(10000) });
    const Receipt ticket = m_core.print->saleReceipt(*m_core.sales->sale(id));
    bool named = false;
    for (const ReceiptLine &line : ticket.lines)
        named = named || (line.left == QStringLiteral("Cliente: ANDE") && line.right == QStringLiteral("80009735-1"));
    QVERIFY(named);
}

QTEST_MAIN(tst_Customers)
#include "tst_customers.moc"
