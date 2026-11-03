// CSV parsing used by the import module: RFC 4180 quoting, separators,
// and the encodings Excel writes.
#include "csvreader.h"

#include <QtTest>

class tst_CsvReader : public QObject
{
    Q_OBJECT

private slots:
    void quotedFields();
    void lineNumbersWithMultilineFields();
    void noTrailingNewline();
    void emptyFields();
    void strayQuoteKept();
    void lineEndings_data();
    void lineEndings();
    void detectSeparator_data();
    void detectSeparator();
    void decode_data();
    void decode();
};

void tst_CsvReader::quotedFields()
{
    const auto recs = Csv::parse("a;\"b;c\";\"say \"\"hi\"\"\"\r\n", ';');
    QCOMPARE(recs.size(), 1);
    QCOMPARE(recs[0].fields, QStringList({ "a", "b;c", "say \"hi\"" }));
}

void tst_CsvReader::lineNumbersWithMultilineFields()
{
    const auto recs = Csv::parse("h1,h2\n\"line1\nline2\",x\nlast,y\n", ',');
    QCOMPARE(recs.size(), 3);
    QCOMPARE(recs[1].fields, QStringList({ "line1\nline2", "x" }));
    QCOMPARE(recs[1].line, 2);      // the line the record starts on
    QCOMPARE(recs[2].line, 4);      // after the two-line field
}

void tst_CsvReader::noTrailingNewline()
{
    const auto recs = Csv::parse("a,b", ',');
    QCOMPARE(recs.size(), 1);
    QCOMPARE(recs[0].fields.size(), 2);
}

void tst_CsvReader::emptyFields()
{
    QCOMPARE(Csv::parse("a,,\n", ',')[0].fields, QStringList({ "a", "", "" }));
}

void tst_CsvReader::strayQuoteKept()
{
    QCOMPARE(Csv::parse("ab\"c,d\n", ',')[0].fields, QStringList({ "ab\"c", "d" }));
}

void tst_CsvReader::lineEndings_data()
{
    QTest::addColumn<QString>("text");
    QTest::newRow("LF")   << "a,b\nc,d\n";
    QTest::newRow("CRLF") << "a,b\r\nc,d\r\n";
    QTest::newRow("CR")   << "a,b\rc,d\r";
}

void tst_CsvReader::lineEndings()
{
    QFETCH(QString, text);
    const auto recs = Csv::parse(text, ',');
    QCOMPARE(recs.size(), 2);
    QCOMPARE(recs[1].fields, QStringList({ "c", "d" }));
}

void tst_CsvReader::detectSeparator_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QChar>("separator");
    QTest::newRow("semicolon")                  << "a;b;c\n" << QChar(';');
    QTest::newRow("comma")                      << "a,b,c\n" << QChar(',');
    QTest::newRow("quoted ; ignored, row 1 only") << "\"x;y\",a,b\n1;2;3;4" << QChar(',');
}

void tst_CsvReader::detectSeparator()
{
    QFETCH(QString, text);
    QFETCH(QChar, separator);
    QCOMPARE(Csv::detectSeparator(text), separator);
}

void tst_CsvReader::decode_data()
{
    QTest::addColumn<QByteArray>("bytes");
    QTest::newRow("UTF-8")          << QByteArray("C\xC3\xB3" "digo");
    QTest::newRow("UTF-8 with BOM") << QByteArray("\xEF\xBB\xBF" "C\xC3\xB3" "digo");
    QTest::newRow("Windows-1252")   << QByteArray("C\xF3" "digo");     // Excel on Spanish Windows
}

void tst_CsvReader::decode()
{
    QFETCH(QByteArray, bytes);
    QCOMPARE(Csv::decode(bytes), QString::fromUtf8("Código"));
}

QTEST_APPLESS_MAIN(tst_CsvReader)
#include "tst_csvreader.moc"
