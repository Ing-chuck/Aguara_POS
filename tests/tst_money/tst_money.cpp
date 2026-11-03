// Money in whole guaraníes: parsing what cashiers type, formatting, IVA.
#include <money.h>

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

QTEST_APPLESS_MAIN(tst_Money)
#include "tst_money.moc"
