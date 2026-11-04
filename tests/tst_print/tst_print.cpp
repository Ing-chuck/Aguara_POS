// Tickets and the drawer: the bytes a thermal printer receives, what a ticket
// says, when it's a copy and when the drawer opens. CoreFixture captures the
// bytes: nothing here reaches a real printer.
#include "escpos.h"
#include "sqluserservice.h"
#include "testsupport.h"

#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

using TestSupport::CoreFixture;
using TestSupport::byVoucher;
using TestSupport::inCash;

namespace {

const QByteArray Pulse("\x1B\x70\x00\x19\xFA", 5);
const QByteArray Cut("\x1D\x56\x42\x00", 4);

CashCount oneNote()
{
    DenominationCount note;
    note.denomination = 100000;
    note.count = 1;
    return { note };
}

/// A job without its commands: the paper, as text.
QStringList paper(QByteArray job)
{
    job.replace("\x1B@", "");
    for (int i = 0; i < 256; ++i) {
        for (const char *prefix : { "\x1Bt", "\x1B" "a", "\x1B" "E", "\x1D!", "\x1B" "d" })
            job.replace(QByteArray(prefix) + char(i), "");
    }
    job.replace(Pulse, "");
    job.replace(Cut, "");
    QStringList lines;
    for (const QByteArray &line : job.split('\n'))
        lines << QString::fromLatin1(line);
    return lines;
}

} // namespace

class tst_Print : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase_data() { TestSupport::addBackendRows(); }

    // bytes
    void accentsAreInCodePage850();
    void controlCharactersCantBecomeCommands();
    void wrapBreaksAtSpaces();
    void rowsFitThePaper_data();
    void rowsFitThePaper();
    void jobStartsCutsAndKicksAsAsked();

    // what a ticket says
    void ticketShowsTheSale();
    void ticketShowsDiscountsAndRounding();
    void ticketShowsEveryPayment();
    void ticketFitsNarrowPaper();

    // charging
    void noPrinterNoJob();
    void cashOpensTheDrawerCardDoesNot();
    void drawerWithoutTicket();
    void failedPrinterLeavesTheSaleAndTheOriginal();
    void outageStillPrintsAndOpensTheDrawer();

    // copies
    void secondTicketIsACopy();
    void voidedSaleIsNotPrinted();
    void pdfOfASale();

    // the rest
    void drawerByHandIsAudited();
    void drawerNeedsAThermalPrinter();
    void commonPrinterMustExist();
    void windowsQueueReportsAMissingPrinter();
    void storeInfoIsShared();
    void printerSettingsStayInTheIni();

private:
    void setPrinter(PrinterKind kind = PrinterKind::Thermal, int columns = 48);
    qint64 sell(const QList<SalePayment> &payments, Money price = 10000);
    bool prepare();
    QString ticket(qint64 id);

    QTemporaryDir m_tmp;
    CoreFixture m_core;
};

void tst_Print::setPrinter(PrinterKind kind, int columns)
{
    PrinterSettings s;
    s.kind = kind;
    s.name = QStringLiteral("TERMICA");
    s.columns = columns;
    m_core.print->savePrinterSettings(s);
}

bool tst_Print::prepare()
{
    Product p;
    p.code = QStringLiteral("A");
    p.description = QStringLiteral("LECHE ENTERA 1L");
    p.price = 10000;
    p.stock = AguaraQuantity::whole(100);
    return m_core.products->saveProduct(p) && m_core.cash->openSession(oneNote());
}

qint64 tst_Print::sell(const QList<SalePayment> &payments, Money price)
{
    SaleLine line;
    line.code = QStringLiteral("A");
    line.description = QStringLiteral("LECHE ENTERA 1L");
    line.unit = QStringLiteral("un");
    line.unitPrice = price;
    SaleDraft draft;
    draft.lines << line;
    return m_core.sales->checkout(draft, payments);
}

/// The ticket, one "left | right" per line.
QString tst_Print::ticket(qint64 id)
{
    QStringList text;
    for (const ReceiptLine &line : m_core.print->saleReceipt(*m_core.sales->sale(id)).lines)
        text << (line.rule ? QStringLiteral("---") : line.left + QStringLiteral(" | ") + line.right);
    return text.join(QLatin1Char('\n'));
}

// --- bytes -------------------------------------------------------------------

void tst_Print::accentsAreInCodePage850()
{
    QCOMPARE(EscPos::encode(QStringLiteral("ñÑáéíóúü¿")), QByteArray("\xA4\xA5\xA0\x82\xA1\xA2\xA3\x81\xA8"));
    QCOMPARE(EscPos::encode(QStringLiteral("Guaraní")), QByteArray("Guaran\xA1"));
    QCOMPARE(EscPos::encode(QStringLiteral("a€b")), QByteArray("a?b"));       // not in 850
}

void tst_Print::controlCharactersCantBecomeCommands()
{
    // a description that would cut the paper or open the drawer
    QCOMPARE(EscPos::encode(QString::fromLatin1("A\x1Bp\x00\x19\xFA" "B", 7)), QByteArray("Ap\xA3" "B"));
    QCOMPARE(EscPos::encode(QStringLiteral("x\ty\r\nz")), QByteArray("xyz"));
}

void tst_Print::wrapBreaksAtSpaces()
{
    QCOMPARE(EscPos::wrap(QStringLiteral("uno dos tres"), 7), QStringList({ "uno dos", "tres" }));
    QCOMPARE(EscPos::wrap(QStringLiteral("  uno   dos "), 20), QStringList({ "uno dos" }));
    QCOMPARE(EscPos::wrap(QStringLiteral("1234567890 a"), 4), QStringList({ "1234", "5678", "90 a" }));
    QCOMPARE(EscPos::wrap(QString(), 4), QStringList({ QString() }));
}

void tst_Print::rowsFitThePaper_data()
{
    QTest::addColumn<int>("columns");
    QTest::newRow("80 mm") << 48;
    QTest::newRow("58 mm") << 32;
}

void tst_Print::rowsFitThePaper()
{
    QFETCH(int, columns);
    Receipt r;
    r.add(QStringLiteral("corto"), QStringLiteral("1.000"));
    r.add(QStringLiteral("una descripción larga que no entra en la línea del ticket"), QStringLiteral("123.456.789"));
    r.rule();
    const QStringList lines = paper(EscPos::render(r, columns, false, false));

    QCOMPARE(lines.first().size(), columns);
    QVERIFY(lines.first().startsWith(QStringLiteral("corto ")));
    QVERIFY(lines.first().endsWith(QStringLiteral(" 1.000")));
    for (const QString &line : lines)
        QVERIFY2(line.size() <= columns, qPrintable(line));
    QVERIFY(lines.contains(QStringLiteral("123.456.789").rightJustified(columns)));     // the amount, on its own
    QVERIFY(lines.contains(QString(columns, QLatin1Char('-'))));
}

void tst_Print::jobStartsCutsAndKicksAsAsked()
{
    Receipt r;
    r.add(QStringLiteral("x"));
    const QByteArray init("\x1B@\x1Bt\x02", 5);

    const QByteArray plain = EscPos::render(r, 48, false, false);
    QVERIFY(plain.startsWith(init));
    QVERIFY(!plain.contains(Pulse));
    QVERIFY(!plain.contains(Cut));

    const QByteArray full = EscPos::render(r, 48, true, true);
    QVERIFY(full.startsWith(init + Pulse));     // the drawer doesn't wait for the paper
    QVERIFY(full.endsWith(Cut));
    QVERIFY(full.indexOf(Pulse) < full.indexOf('x'));
}

// --- what a ticket says ------------------------------------------------------

void tst_Print::ticketShowsTheSale()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(prepare());
    StoreInfo store;
    store.name = QStringLiteral("ALMACEN LA ESQUINA");
    store.ruc = QStringLiteral("80012345-6");
    store.address = QStringLiteral("Av. Mcal. López 123");
    store.phone = QStringLiteral("021 555 123");
    store.footer = QStringLiteral("Gracias por su compra\nVuelva pronto");
    QVERIFY(m_core.print->saveStoreInfo(store));

    const QString text = ticket(sell({ inCash(20000) }));
    QVERIFY2(text.contains(QStringLiteral("ALMACEN LA ESQUINA")), qPrintable(text));
    QVERIFY(text.contains(QStringLiteral("RUC 80012345-6")));
    QVERIFY(text.contains(QStringLiteral("Tel. 021 555 123")));
    QVERIFY(text.contains(QStringLiteral("Ticket 001-000001")));
    QVERIFY(text.contains(QStringLiteral("LECHE ENTERA 1L")));
    QVERIFY(text.contains(QStringLiteral("  1 un x 10.000  IVA 10% | 10.000")));
    QVERIFY(text.contains(QStringLiteral("TOTAL Gs. | 10.000")));
    QVERIFY(text.contains(QStringLiteral("Gravado 10% (IVA 909) | 10.000")));
    QVERIFY(text.contains(QStringLiteral("Efectivo | 20.000")));
    QVERIFY(text.contains(QStringLiteral("Vuelto | 10.000")));
    QVERIFY(text.contains(QStringLiteral("Vuelva pronto")));
    QVERIFY(text.contains(QStringLiteral("No válido como factura")));   // internal until phase 8
    QVERIFY(!text.contains(QStringLiteral("COPIA")));
    QVERIFY(!text.contains(QStringLiteral("Redondeo")));
    QVERIFY(!text.contains(QStringLiteral("Subtotal")));
}

void tst_Print::ticketShowsDiscountsAndRounding()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(prepare());
    SaleLine line;
    line.code = QStringLiteral("A");
    line.description = QStringLiteral("QUESO");
    line.unit = QStringLiteral("kg");
    line.decimals = 3;
    line.unitPrice = 4500;
    line.quantity = 1250;                       // 5.625
    line.discount = 100;
    SaleDraft draft;
    draft.lines << line;

    const QString text = ticket(m_core.sales->checkout(draft, { inCash(6000) }));
    QVERIFY2(text.contains(QStringLiteral("  1,250 kg x 4.500  IVA 10% | 5.625")), qPrintable(text));
    QVERIFY(text.contains(QStringLiteral("  Descuento | -100")));
    QVERIFY(text.contains(QStringLiteral("Subtotal | 5.625")));
    QVERIFY(text.contains(QStringLiteral("Exacto | 5.525")));
    QVERIFY(text.contains(QStringLiteral("Redondeo en efectivo | 25")));
    QVERIFY(text.contains(QStringLiteral("TOTAL Gs. | 5.550")));
}

void tst_Print::ticketShowsEveryPayment()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(prepare());
    const QString text = ticket(sell({ byVoucher(PaymentMethod::Debit, 6000, QStringLiteral("448811")),
                                       byVoucher(PaymentMethod::Transfer, 1000, QStringLiteral("T-9")), inCash(5000) }));
    QVERIFY2(text.contains(QStringLiteral("Tarjeta de débito 448811 | 6.000")), qPrintable(text));
    QVERIFY(text.contains(QStringLiteral("Transferencia T-9 | 1.000")));
    QVERIFY(text.contains(QStringLiteral("Efectivo | 5.000")));
    QVERIFY(text.contains(QStringLiteral("Vuelto | 2.000")));
}

void tst_Print::ticketFitsNarrowPaper()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(prepare());
    setPrinter(PrinterKind::Thermal, 32);
    QVERIFY(m_core.print->finishSale(sell({ byVoucher(PaymentMethod::Credit, 10000, QStringLiteral("123456789012")) })));
    QCOMPARE(m_core.printed.size(), 1);
    for (const QString &line : paper(m_core.printed.first()))
        QVERIFY2(line.size() <= 32, qPrintable(line));
}

// --- charging ----------------------------------------------------------------

void tst_Print::noPrinterNoJob()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(prepare());
    const qint64 id = sell({ inCash(10000) });
    QVERIFY(m_core.print->finishSale(id));      // nothing to do is not a failure
    QVERIFY(m_core.printed.isEmpty());
    QCOMPARE(m_core.sales->sale(id)->printCount, 0);
}

void tst_Print::cashOpensTheDrawerCardDoesNot()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(prepare());
    setPrinter();

    const qint64 cash = sell({ inCash(10000) });
    QVERIFY(m_core.print->finishSale(cash));
    QCOMPARE(m_core.printed.size(), 1);
    QVERIFY(m_core.printed.at(0).contains(Pulse));
    QCOMPARE(m_core.sales->sale(cash)->printCount, 1);

    QVERIFY(m_core.print->finishSale(sell({ byVoucher(PaymentMethod::Debit, 10000, QStringLiteral("1")) })));
    QCOMPARE(m_core.printed.size(), 2);
    QVERIFY(!m_core.printed.at(1).contains(Pulse));

    // mixed: cash came in
    QVERIFY(m_core.print->finishSale(sell({ byVoucher(PaymentMethod::Debit, 5000, QStringLiteral("2")), inCash(5000) })));
    QVERIFY(m_core.printed.at(2).contains(Pulse));

    PrinterSettings s = m_core.print->printerSettings();
    s.openDrawerOnCash = false;
    m_core.print->savePrinterSettings(s);
    QVERIFY(m_core.print->finishSale(sell({ inCash(10000) })));
    QVERIFY(!m_core.printed.at(3).contains(Pulse));
}

void tst_Print::drawerWithoutTicket()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(prepare());
    PrinterSettings s;
    s.kind = PrinterKind::Thermal;
    s.name = QStringLiteral("TERMICA");
    s.printEverySale = false;
    m_core.print->savePrinterSettings(s);

    const qint64 id = sell({ inCash(10000) });
    QVERIFY(m_core.print->finishSale(id));
    QCOMPARE(m_core.printed, QList<QByteArray>({ Pulse }));
    QCOMPARE(m_core.sales->sale(id)->printCount, 0);            // no ticket yet: the first is still the original

    QVERIFY(m_core.print->finishSale(sell({ byVoucher(PaymentMethod::Debit, 10000, QStringLiteral("1")) })));
    QCOMPARE(m_core.printed.size(), 1);
}

void tst_Print::failedPrinterLeavesTheSaleAndTheOriginal()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(prepare());
    setPrinter();
    m_core.printerFails = true;

    const qint64 id = sell({ inCash(10000) });
    QVERIFY(!m_core.print->finishSale(id));
    QVERIFY(m_core.print->lastError().contains(QStringLiteral("no responde")));
    const auto sale = m_core.sales->sale(id);
    QCOMPARE(sale->state, QLatin1String(SaleState::Paid));
    QCOMPARE(sale->printCount, 0);

    m_core.printerFails = false;
    QVERIFY(m_core.print->printSale(id));       // paper back in: this one is the original
    QVERIFY(!m_core.printed.first().contains("COPIA"));
}

void tst_Print::outageStillPrintsAndOpensTheDrawer()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(prepare());
    setPrinter();
    StoreInfo store;
    store.name = QStringLiteral("ALMACEN LA ESQUINA");
    QVERIFY(m_core.print->saveStoreInfo(store));
    QCOMPARE(sell({ inCash(10000) }) > 0, true);

    m_core.monitor->simulateOutage(true);
    const qint64 id = sell({ inCash(20000) });
    QVERIFY(id < 0);
    QVERIFY2(m_core.print->finishSale(id), qPrintable(m_core.print->lastError()));

    QCOMPARE(m_core.printed.size(), 1);
    const QByteArray job = m_core.printed.first();
    QVERIFY(job.contains("ALMACEN LA ESQUINA"));
    QVERIFY(job.contains("Ticket 001-000002"));
    QVERIFY(job.contains(Pulse));
    QCOMPARE(m_core.sales->sale(id)->printCount, 1);

    m_core.monitor->simulateOutage(false);
    QVERIFY(TestSupport::waitFor([&] { return m_core.sales->pendingSales().isEmpty(); }));
    const auto saved = m_core.sales->sessionSales(m_core.cash->currentSession()->id);
    QCOMPARE(saved.first().printCount, 1);      // a later ticket is a copy
}

// --- copies ------------------------------------------------------------------

void tst_Print::secondTicketIsACopy()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    SqlUserService users(m_core.monitor.get());
    m_core.print->setUsers(&users);
    QVERIFY(users.createFirstAdmin(QStringLiteral("admin"), QStringLiteral("Admin"), QStringLiteral("secreto1")));
    QVERIFY(prepare());
    setPrinter();

    const qint64 id = sell({ inCash(10000) });
    QVERIFY(m_core.print->finishSale(id));
    QVERIFY(!m_core.printed.at(0).contains("COPIA"));

    QVERIFY(m_core.print->printSale(id));
    QCOMPARE(m_core.printed.at(1).count("*** COPIA ***"), 2);   // top and bottom
    QVERIFY(!m_core.printed.at(1).contains(Pulse));             // a copy doesn't open the drawer
    QCOMPARE(m_core.sales->sale(id)->printCount, 2);

    int audited = 0;
    for (const AuditEntry &e : users.auditEntries())
        audited += e.action == QLatin1String("venta.reimprimir") ? 1 : 0;
    QCOMPARE(audited, 1);
    m_core.print->setUsers(nullptr);
}

void tst_Print::voidedSaleIsNotPrinted()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(prepare());
    setPrinter();
    const qint64 id = sell({ inCash(10000) });
    QVERIFY(m_core.sales->voidSale(id, QStringLiteral("Error")));

    QVERIFY(!m_core.print->printSale(id));
    QVERIFY(!m_core.print->saveSalePdf(id, m_tmp.filePath(QStringLiteral("anulada.pdf"))));
    QVERIFY(m_core.printed.isEmpty());
}

void tst_Print::pdfOfASale()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(prepare());
    const qint64 id = sell({ inCash(10000) });
    const QString file = m_tmp.filePath(QStringLiteral("ticket.pdf"));
    QVERIFY2(m_core.print->saveSalePdf(id, file), qPrintable(m_core.print->lastError()));

    QFile pdf(file);
    QVERIFY(pdf.open(QIODevice::ReadOnly));
    QVERIFY(pdf.read(4) == "%PDF");
    pdf.close();
    QCOMPARE(m_core.sales->sale(id)->printCount, 1);            // a ticket went out
    QVERIFY(QFile::remove(file));
}

// --- the rest ----------------------------------------------------------------

void tst_Print::drawerByHandIsAudited()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    SqlUserService users(m_core.monitor.get());
    m_core.print->setUsers(&users);
    QVERIFY(users.createFirstAdmin(QStringLiteral("admin"), QStringLiteral("Admin"), QStringLiteral("secreto1")));
    setPrinter();

    QVERIFY(m_core.print->openDrawer());
    QCOMPARE(m_core.printed, QList<QByteArray>({ Pulse }));
    bool audited = false;
    for (const AuditEntry &e : users.auditEntries())
        audited = audited || e.action == QLatin1String("caja.abrir_cajon");
    QVERIFY(audited);
    m_core.print->setUsers(nullptr);
}

void tst_Print::drawerNeedsAThermalPrinter()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(!m_core.print->openDrawer());       // no printer
    setPrinter(PrinterKind::System);
    QVERIFY(!m_core.print->openDrawer());
    QVERIFY(m_core.print->lastError().contains(QStringLiteral("térmica")));
    QVERIFY(m_core.printed.isEmpty());
}

/// Never falls back to the Windows default printer, which could be anybody's.
void tst_Print::commonPrinterMustExist()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    PrinterSettings s;
    s.kind = PrinterKind::System;
    s.name = QStringLiteral("AguaraPOS impresora que no existe");
    QVERIFY(!m_core.print->printTest(s));
    QVERIFY(m_core.print->lastError().contains(QStringLiteral("no está instalada")));
    s.name.clear();
    QVERIFY(!m_core.print->printTest(s));
}

void tst_Print::windowsQueueReportsAMissingPrinter()
{
    QString error;
    QVERIFY(!SqlPrintService::sendToWindowsQueue(QStringLiteral("AguaraPOS impresora que no existe"), "x", &error));
    QVERIFY(error.contains(QStringLiteral("No se pudo imprimir")));
}

void tst_Print::storeInfoIsShared()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(m_core.print->storeInfo().name.isEmpty());
    StoreInfo store;
    store.name = QStringLiteral("  Almacén Ñandú  ");
    store.footer = QStringLiteral("línea 1\nlínea 2");
    QVERIFY2(m_core.print->saveStoreInfo(store), qPrintable(m_core.print->lastError()));
    store.ruc = QStringLiteral("80012345-6");
    QVERIFY(m_core.print->saveStoreInfo(store));                // a second time: updates

    // another register, same database
    SqlPrintService other(m_core.monitor.get(), m_core.sales.get(), m_core.cash.get());
    const StoreInfo read = other.storeInfo();
    QCOMPARE(read.name, QStringLiteral("Almacén Ñandú"));
    QCOMPARE(read.ruc, QStringLiteral("80012345-6"));
    QCOMPARE(read.footer, QStringLiteral("línea 1\nlínea 2"));
    QVERIFY(read.address.isEmpty());
}

void tst_Print::printerSettingsStayInTheIni()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    {
        QSettings ini(DatabaseSettings::settingsPath(), QSettings::IniFormat);
        ini.setValue(QStringLiteral("session/lock_minutes"), 3);
    }
    QCOMPARE(m_core.print->printerSettings().kind, PrinterKind::None);

    PrinterSettings s;
    s.kind = PrinterKind::Thermal;
    s.name = QStringLiteral("EPSON TM-T20");
    s.columns = 32;
    s.cut = false;
    s.printEverySale = false;
    s.openDrawerOnCash = false;
    m_core.print->savePrinterSettings(s);

    const PrinterSettings read = m_core.print->printerSettings();
    QCOMPARE(read.kind, PrinterKind::Thermal);
    QCOMPARE(read.name, s.name);
    QCOMPARE(read.columns, 32);
    QVERIFY(!read.cut);
    QVERIFY(!read.printEverySale);
    QVERIFY(!read.openDrawerOnCash);
    QCOMPARE(QSettings(DatabaseSettings::settingsPath(), QSettings::IniFormat)
                 .value(QStringLiteral("session/lock_minutes")).toInt(), 3);
}

QTEST_MAIN(tst_Print)
#include "tst_print.moc"
