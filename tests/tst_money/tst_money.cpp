// Money in whole guaraníes: parsing what cashiers type, formatting, IVA.
#include <barcode.h>
#include <money.h>
#include <quantity.h>

#include <QtTest>

class tst_Money : public QObject
{
    Q_OBJECT

private slots:
    void parse_data();
    void parse();
    void format();
    void vatIncluded_data();
    void vatIncluded();
    void parseQuantity_data();
    void parseQuantity();
    void formatQuantity_data();
    void formatQuantity();
    void lineTotal();
    void ean13();
    void scaleCode();
};

void tst_Money::parse_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<bool>("valid");
    QTest::addColumn<qint64>("value");

    QTest::newRow("plain")              << "15000"          << true  << qint64(15000);
    QTest::newRow("dot thousands")      << "15.000"         << true  << qint64(15000);
    QTest::newRow("comma thousands")    << "15,000"         << true  << qint64(15000);
    QTest::newRow("symbol, millions")   << "Gs. 1.500.000"  << true  << qint64(1500000);
    QTest::newRow("decimal zeros")      << "15000.00"       << true  << qint64(15000);
    QTest::newRow("half rounds up")     << "15.000,50"      << true  << qint64(15001);
    QTest::newRow("below half down")    << "15.000,49"      << true  << qint64(15000);
    QTest::newRow("one decimal")        << "1.5"            << true  << qint64(2);
    QTest::newRow("letters")            << "abc"            << false << qint64(0);
    QTest::newRow("bad grouping")       << "1.50.0"         << false << qint64(0);
    QTest::newRow("empty")              << ""               << false << qint64(0);
}

void tst_Money::parse()
{
    QFETCH(QString, text);
    QFETCH(bool, valid);
    QFETCH(qint64, value);

    Money m = -999;
    QCOMPARE(AguaraMoney::parse(text, &m), valid);
    if (valid)
        QCOMPARE(m, value);
}

void tst_Money::format()
{
    QCOMPARE(AguaraMoney::format(0), QStringLiteral("0"));
    QCOMPARE(AguaraMoney::format(15000), QStringLiteral("15.000"));
    QCOMPARE(AguaraMoney::format(1500000), QStringLiteral("1.500.000"));
    QCOMPARE(AguaraMoney::format(Money(3000000000LL)), QStringLiteral("3.000.000.000"));   // > 32-bit
}

void tst_Money::vatIncluded_data()
{
    QTest::addColumn<qint64>("gross");
    QTest::addColumn<int>("rate");
    QTest::addColumn<qint64>("vat");

    QTest::newRow("10%")       << qint64(11000) << 10 << qint64(1000);
    QTest::newRow("5% rounds") << qint64(5000)  << 5  << qint64(238);
    QTest::newRow("exempt")    << qint64(5000)  << 0  << qint64(0);
}

void tst_Money::vatIncluded()
{
    QFETCH(qint64, gross);
    QFETCH(int, rate);
    QFETCH(qint64, vat);
    QCOMPARE(AguaraMoney::vatIncluded(gross, rate), vat);
}

void tst_Money::parseQuantity_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<bool>("valid");
    QTest::addColumn<qint64>("value");

    QTest::newRow("whole")              << "24"        << true  << qint64(24000);
    QTest::newRow("decimal comma")      << "2,75"      << true  << qint64(2750);
    QTest::newRow("decimal dot")        << "2.75"      << true  << qint64(2750);
    QTest::newRow("three decimals")     << "1,250"     << true  << qint64(1250);
    QTest::newRow("dot is thousands")   << "1.500"     << true  << qint64(1500000);
    QTest::newRow("both")               << "1.500,5"   << true  << qint64(1500500);
    QTest::newRow("negative")           << "-3"        << true  << qint64(-3000);
    QTest::newRow("spaces")             << " 5 "       << true  << qint64(5000);
    QTest::newRow("too many decimals")  << "1,2345"    << false << qint64(0);
    QTest::newRow("two commas")         << "1,2,3"     << false << qint64(0);
    QTest::newRow("letters")            << "abc"       << false << qint64(0);
    QTest::newRow("empty")              << ""          << false << qint64(0);
}

void tst_Money::parseQuantity()
{
    QFETCH(QString, text);
    QFETCH(bool, valid);
    QFETCH(qint64, value);
    Quantity q = -999;
    QCOMPARE(AguaraQuantity::parse(text, &q), valid);
    if (valid)
        QCOMPARE(q, value);
}

void tst_Money::formatQuantity_data()
{
    QTest::addColumn<qint64>("value");
    QTest::addColumn<int>("decimals");
    QTest::addColumn<QString>("text");

    QTest::newRow("units")              << qint64(24000)   << 0 << "24";
    QTest::newRow("thousands")          << qint64(1500000) << 0 << "1.500";
    QTest::newRow("kg")                 << qint64(2750)    << 3 << "2,750";
    QTest::newRow("whole kg")           << qint64(2000)    << 3 << "2,000";
    QTest::newRow("hidden nothing")     << qint64(2500)    << 0 << "2,5";
    QTest::newRow("negative")           << qint64(-1250)   << 3 << "-1,250";
    QTest::newRow("zero")               << qint64(0)       << 0 << "0";
}

void tst_Money::formatQuantity()
{
    QFETCH(qint64, value);
    QFETCH(int, decimals);
    QFETCH(QString, text);
    QCOMPARE(AguaraQuantity::format(value, decimals), text);
}

void tst_Money::lineTotal()
{
    QCOMPARE(AguaraQuantity::lineTotal(15000, AguaraQuantity::whole(3)), qint64(45000));
    QCOMPARE(AguaraQuantity::lineTotal(40000, 1250), qint64(50000));    // 1,250 kg at 40.000
    QCOMPARE(AguaraQuantity::lineTotal(999, 1500), qint64(1499));       // 1498,5 rounds up
    QCOMPARE(AguaraQuantity::lineTotal(1000, -2000), qint64(-2000));
}

void tst_Money::ean13()
{
    QCOMPARE(AguaraBarcode::ean13CheckDigit(QStringLiteral("400638133393")), 1);  // a published example
    QVERIFY(AguaraBarcode::isValidEan13(QStringLiteral("4006381333931")));
    QVERIFY(!AguaraBarcode::isValidEan13(QStringLiteral("4006381333932")));
    QVERIFY(!AguaraBarcode::isValidEan13(QStringLiteral("400638133393")));
    QVERIFY(!AguaraBarcode::isValidEan13(QStringLiteral("400638133393x")));

    const QString bars = AguaraBarcode::ean13Bars(QStringLiteral("4006381333931"));
    QCOMPARE(bars.size(), 95);
    QVERIFY(bars.startsWith(QStringLiteral("101")));
    QVERIFY(bars.endsWith(QStringLiteral("101")));
    QCOMPARE(bars.mid(45, 5), QStringLiteral("01010"));         // the centre guard
    QVERIFY(AguaraBarcode::ean13Bars(QStringLiteral("123")).isEmpty());
}

void tst_Money::scaleCode()
{
    const QString weight = QStringLiteral("201234501250");
    const AguaraBarcode::ScaleCode a = AguaraBarcode::parseScaleCode(
        weight + QString::number(AguaraBarcode::ean13CheckDigit(weight)));
    QVERIFY(a.valid);
    QCOMPARE(a.articleCode, QStringLiteral("2012345"));
    QCOMPARE(a.grams, qint64(1250));
    QCOMPARE(a.price, qint64(0));

    const QString price = QStringLiteral("285432125000");
    const AguaraBarcode::ScaleCode b = AguaraBarcode::parseScaleCode(
        price + QString::number(AguaraBarcode::ean13CheckDigit(price)));
    QVERIFY(b.valid);
    QCOMPARE(b.price, qint64(25000));
    QCOMPARE(b.grams, qint64(0));

    QVERIFY(!AguaraBarcode::parseScaleCode(QStringLiteral("4006381333931")).valid);   // not a 2x prefix
}

QTEST_APPLESS_MAIN(tst_Money)
#include "tst_money.moc"
