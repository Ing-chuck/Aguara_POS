// Customers: the RUC's check digit, the rules of the list, and the customer
// of a sale, also held or in the queue.
#include "testsupport.h"

#include <formcheck.h>
#include <livesearch.h>

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
    void optionalDataHasToMakeSense();
    void searchForgivesAccentsAndOrder();
    void quantitiesFitTheirUnit();
    void savingChecksTheContactData();

    void saveAndFind();
    void documentIsNotRepeated();
    void manyWithoutADocument();
    void leavingKeepsTheCustomer();
    void nameIsRequired();
    void importIsAllOrNothing();
    void everyCustomerIsListedForAnImport();

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

void tst_Customers::optionalDataHasToMakeSense()
{
    for (const char *good : { "", "  ", "0981 123 456", "(021) 555-123", "+595 981 123456", "021 555 / 021 556" })
        QVERIFY2(AguaraForm::validPhone(QString::fromUtf8(good)), good);
    for (const char *bad : { "ver contrato", "123", "0981-CASA", "1234567890123456789012345678901" })
        QVERIFY2(!AguaraForm::validPhone(QString::fromUtf8(bad)), bad);

    for (const char *good : { "", "ventas@comercio.com.py", " ana.perez@gmail.com " })
        QVERIFY2(AguaraForm::validEmail(QString::fromUtf8(good)), good);
    for (const char *bad : { "ventas", "ventas@comercio", "@comercio.com", "a b@c.com", "a@b..com" })
        QVERIFY2(!AguaraForm::validEmail(QString::fromUtf8(bad)), bad);

    for (const char *good : { "", "www.comercio.com.py", "https://comercio.com/tienda" })
        QVERIFY2(AguaraForm::validWeb(QString::fromUtf8(good)), good);
    for (const char *bad : { "comercio", "con espacios.com" })
        QVERIFY2(!AguaraForm::validWeb(QString::fromUtf8(bad)), bad);

    QVERIFY(AguaraForm::validUsername(QStringLiteral("  maria.perez ")));
    QVERIFY(!AguaraForm::validUsername(QStringLiteral("maria perez")));
    QVERIFY(AguaraForm::validCode(QStringLiteral("7840001-A")));
    QVERIFY(!AguaraForm::validCode(QString()));
    QVERIFY(!AguaraForm::validCode(QStringLiteral("78 40")));
    QVERIFY(!AguaraForm::validCode(QString(21, QLatin1Char('1'))));
}

void tst_Customers::searchForgivesAccentsAndOrder()
{
    QVERIFY(AguaraSearch::matches(QStringLiteral("azucar kilo"), QStringLiteral("AZÚCAR BLANCA 1 KILO")));
    QVERIFY(AguaraSearch::matches(QStringLiteral("kilo AZÚCAR"), QStringLiteral("azucar blanca 1 kilo")));
    QVERIFY(AguaraSearch::matches(QStringLiteral("  "), QStringLiteral("cualquier cosa")));
    QVERIFY(AguaraSearch::matches(QStringLiteral("nandu"), QStringLiteral("ÑANDÚ")));
    QVERIFY(!AguaraSearch::matches(QStringLiteral("azucar negra"), QStringLiteral("AZÚCAR BLANCA")));

    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    Customer c;
    c.name = QStringLiteral("GONZÁLEZ PÉREZ, Ana");
    c.documentType = QString::fromLatin1(DocumentType::Ci);
    c.document = QStringLiteral("1234567");
    QVERIFY(m_core.customers->saveCustomer(c) != 0);
    QCOMPARE(m_core.customers->customers(QStringLiteral("gonzalez")).size(), 1);
    QCOMPARE(m_core.customers->customers(QStringLiteral("perez ana")).size(), 1);
    QCOMPARE(m_core.customers->customers(QStringLiteral("4567")).size(), 1);
    QCOMPARE(m_core.customers->customers(QStringLiteral("gomez")).size(), 0);
}

void tst_Customers::quantitiesFitTheirUnit()
{
    QVERIFY(AguaraForm::fitsUnit(AguaraQuantity::whole(3), 0));
    QVERIFY(!AguaraForm::fitsUnit(1500, 0));        // 1,5 of something sold by the unit
    QVERIFY(AguaraForm::fitsUnit(1500, 1));
    QVERIFY(!AguaraForm::fitsUnit(1250, 1));
    QVERIFY(AguaraForm::fitsUnit(1250, 3));
}

void tst_Customers::savingChecksTheContactData()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    Customer c;
    c.name = QStringLiteral("ANA");
    c.phone = QStringLiteral("ver contrato");
    QCOMPARE(m_core.customers->saveCustomer(c), qint64(0));
    QVERIFY(m_core.customers->lastError().contains(QStringLiteral("teléfono")));
    c.phone = QStringLiteral("0981 123 456");
    c.email = QStringLiteral("ana");
    QCOMPARE(m_core.customers->saveCustomer(c), qint64(0));
    QVERIFY(m_core.customers->lastError().contains(QStringLiteral("correo")));
    c.email = QStringLiteral("ana@correo.com");
    c.documentType = QString::fromLatin1(DocumentType::Ci);
    QCOMPARE(m_core.customers->saveCustomer(c), qint64(0));         // a type without its number
    QVERIFY(m_core.customers->lastError().contains(QStringLiteral("número")));
    c.documentType.clear();
    QVERIFY(m_core.customers->saveCustomer(c) != 0);
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

void tst_Customers::importIsAllOrNothing()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    Customer good;
    good.name = QStringLiteral("Bien");
    Customer bad;
    bad.name = QStringLiteral("Mal");
    bad.documentType = QString::fromLatin1(DocumentType::Ruc);
    bad.document = QStringLiteral("80009735-9");
    int saved = -1;
    QVERIFY(!m_core.customers->importCustomers({ good, bad }, &saved));
    QCOMPARE(saved, 0);
    QVERIFY2(m_core.customers->lastError().contains(QStringLiteral("Mal")), qPrintable(m_core.customers->lastError()));
    QVERIFY(m_core.customers->customers().isEmpty());

    QVERIFY(m_core.customers->importCustomers({ good, ande() }, &saved));
    QCOMPARE(saved, 2);
    QCOMPARE(m_core.customers->customers().size(), 2);
}

/// An import has to find each one: a search stops at 500.
void tst_Customers::everyCustomerIsListedForAnImport()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QList<Customer> many;
    for (int i = 0; i < 600; ++i) {
        Customer c;
        c.name = QStringLiteral("Cliente %1").arg(i);
        many << c;
    }
    QVERIFY2(m_core.customers->importCustomers(many), qPrintable(m_core.customers->lastError()));
    QCOMPARE(m_core.customers->customers().size(), 500);
    const auto all = m_core.customers->allCustomers();
    QVERIFY(all.has_value());
    QCOMPARE(all->size(), 600);
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
