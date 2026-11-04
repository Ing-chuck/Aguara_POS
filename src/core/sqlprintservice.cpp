#include "sqlprintservice.h"
#include "connectionmonitor.h"
#include "database.h"
#include "escpos.h"
#include "logging.h"
#include "sqlcashservice.h"
#include "sqlsalesservice.h"

#include <QFile>
#include <QPrinter>
#include <QPrinterInfo>
#include <QSettings>
#include <QSqlQuery>
#include <QTextDocument>
#include <QVariant>

#ifdef Q_OS_WIN
#include <windows.h>
#include <winspool.h>
#endif

namespace {

const char *const kStoreName    = "comercio.nombre";
const char *const kStoreRuc     = "comercio.ruc";
const char *const kStoreAddress = "comercio.direccion";
const char *const kStorePhone   = "comercio.telefono";
const char *const kStoreFooter  = "comercio.pie";

QString kindName(PrinterKind kind)
{
    switch (kind) {
    case PrinterKind::Thermal: return QStringLiteral("termica");
    case PrinterKind::System:  return QStringLiteral("comun");
    default:                   return QStringLiteral("ninguna");
    }
}

PrinterKind kindFromName(const QString &name)
{
    if (name == QLatin1String("termica"))
        return PrinterKind::Thermal;
    return name == QLatin1String("comun") ? PrinterKind::System : PrinterKind::None;
}

} // namespace

SqlPrintService::SqlPrintService(ConnectionMonitor *monitor, SqlSalesService *sales, SqlCashService *cash,
                                 QObject *parent)
    : QObject(parent), m_monitor(monitor), m_sales(sales), m_cash(cash)
    , m_connectionName(monitor->connectionName())
    , m_rawSender(&SqlPrintService::sendToWindowsQueue)
{
}

QSqlDatabase SqlPrintService::db() const
{
    return QSqlDatabase::database(m_connectionName, false);
}

bool SqlPrintService::exec(QSqlQuery &q) const
{
    if (m_monitor->exec(q, false))
        return true;
    m_lastError = m_monitor->lastError();
    return false;
}

// --- settings ----------------------------------------------------------------

StoreInfo SqlPrintService::storeInfo() const
{
    StoreInfo store;
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT clave, valor FROM configuracion WHERE clave LIKE 'comercio.%'"));
    if (!exec(q))
        return m_knownStore;
    while (q.next()) {
        const QString key = q.value(0).toString();
        const QString value = q.value(1).toString();
        if (key == QLatin1String(kStoreName))
            store.name = value;
        else if (key == QLatin1String(kStoreRuc))
            store.ruc = value;
        else if (key == QLatin1String(kStoreAddress))
            store.address = value;
        else if (key == QLatin1String(kStorePhone))
            store.phone = value;
        else if (key == QLatin1String(kStoreFooter))
            store.footer = value;
    }
    m_knownStore = store;
    return store;
}

bool SqlPrintService::saveStoreInfo(const StoreInfo &store)
{
    // One statement: all of it or none of it, on both databases
    QSqlQuery q(db());
    q.prepare(QStringLiteral(
        "INSERT INTO configuracion (clave, valor) VALUES (?, ?), (?, ?), (?, ?), (?, ?), (?, ?)"
        " ON CONFLICT (clave) DO UPDATE SET valor = excluded.valor"));
    const QList<std::pair<const char *, QString>> values = {
        { kStoreName, store.name.trimmed() }, { kStoreRuc, store.ruc.trimmed() },
        { kStoreAddress, store.address.trimmed() }, { kStorePhone, store.phone.trimmed() },
        { kStoreFooter, store.footer.trimmed() },
    };
    for (const auto &value : values) {
        q.addBindValue(QString::fromLatin1(value.first));
        q.addBindValue(value.second.isNull() ? QStringLiteral("") : value.second);
    }
    if (!exec(q))
        return false;
    if (m_users)
        m_users->audit(QStringLiteral("comercio.modificar"), QStringLiteral("configuracion"), QStringLiteral("comercio"));
    return true;
}

PrinterSettings SqlPrintService::printerSettings() const
{
    QSettings ini(DatabaseSettings::settingsPath(), QSettings::IniFormat);
    ini.beginGroup(QStringLiteral("printer"));
    PrinterSettings s;
    s.kind = kindFromName(ini.value(QStringLiteral("kind")).toString());
    s.name = ini.value(QStringLiteral("name")).toString();
    s.columns = ini.value(QStringLiteral("columns"), s.columns).toInt();
    s.cut = ini.value(QStringLiteral("cut"), s.cut).toBool();
    s.printEverySale = ini.value(QStringLiteral("print_every_sale"), s.printEverySale).toBool();
    s.openDrawerOnCash = ini.value(QStringLiteral("open_drawer_on_cash"), s.openDrawerOnCash).toBool();
    return s;
}

void SqlPrintService::savePrinterSettings(const PrinterSettings &s)
{
    QSettings ini(DatabaseSettings::settingsPath(), QSettings::IniFormat);
    ini.beginGroup(QStringLiteral("printer"));
    ini.setValue(QStringLiteral("kind"), kindName(s.kind));
    ini.setValue(QStringLiteral("name"), s.name);
    ini.setValue(QStringLiteral("columns"), s.columns);
    ini.setValue(QStringLiteral("cut"), s.cut);
    ini.setValue(QStringLiteral("print_every_sale"), s.printEverySale);
    ini.setValue(QStringLiteral("open_drawer_on_cash"), s.openDrawerOnCash);
}

QStringList SqlPrintService::printers() const
{
    return QPrinterInfo::availablePrinterNames();
}

// --- what a ticket says ------------------------------------------------------

Receipt SqlPrintService::saleReceipt(const SaleRecord &sale)
{
    const StoreInfo store = storeInfo();
    const SaleTotals &t = sale.totals;
    const QString copy = tr("*** COPIA ***");
    const auto money = [](Money amount) { return AguaraMoney::format(amount); };

    Receipt r;
    if (!store.name.isEmpty())
        r.center(store.name, ReceiptLine::Title);
    if (!store.ruc.isEmpty())
        r.center(tr("RUC %1").arg(store.ruc));
    if (!store.address.isEmpty())
        r.center(store.address);
    if (!store.phone.isEmpty())
        r.center(tr("Tel. %1").arg(store.phone));
    if (sale.printCount > 0)
        r.center(copy, ReceiptLine::Strong);
    r.rule();
    r.add(tr("Ticket %1").arg(AguaraSale::formatNumber(sale.registerId, sale.number)), m_cash->currentRegister().name);
    r.add(sale.when.toString(QStringLiteral("dd/MM/yyyy HH:mm")), tr("Cajero: %1").arg(sale.user));
    r.rule();

    for (const SaleItem &item : t.items) {
        r.add(item.description);
        const QString quantity = AguaraQuantity::format(item.quantity, item.decimals);
        r.add(tr("  %1 %2 x %3  IVA %4")
                  .arg(quantity, item.unit, money(item.unitPrice),
                       item.vatRate == 0 ? tr("exento") : QStringLiteral("%1%").arg(item.vatRate)),
              money(item.gross));
        if (item.discount != 0)
            r.add(tr("  Descuento"), money(-item.discount));
    }
    r.rule();

    if (t.discount != 0) {
        r.add(tr("Subtotal"), money(t.gross));
        r.add(tr("Descuentos"), money(-t.discount));
    }
    if (t.rounding != 0) {
        r.add(tr("Exacto"), money(t.net));
        r.add(tr("Redondeo en efectivo"), money(t.rounding));
    }
    r.add(tr("TOTAL Gs."), money(t.total), ReceiptLine::Total);
    r.rule();

    if (t.vat10 != 0 || t.taxed10 != 0)
        r.add(tr("Gravado 10% (IVA %1)").arg(money(t.vat10)), money(t.taxed10));
    if (t.vat5 != 0 || t.taxed5 != 0)
        r.add(tr("Gravado 5% (IVA %1)").arg(money(t.vat5)), money(t.taxed5));
    if (t.exempt != 0)
        r.add(tr("Exento"), money(t.exempt));
    r.rule();

    for (const SalePayment &p : sale.payments) {
        const QString name = AguaraSale::methodName(p.method);
        r.add(p.reference.isEmpty() ? name : QStringLiteral("%1 %2").arg(name, p.reference), money(p.amount));
        if (p.change != 0)
            r.add(tr("Vuelto"), money(p.change), ReceiptLine::Strong);
    }
    r.rule();

    if (!store.footer.isEmpty()) {
        for (const QString &line : store.footer.split(QLatin1Char('\n')))
            r.center(line);
    }
    r.center(tr("Comprobante interno. No válido como factura."));
    if (sale.printCount > 0)
        r.center(copy, ReceiptLine::Strong);
    return r;
}

QString SqlPrintService::receiptHtml(const Receipt &receipt)
{
    QString rows;
    for (const ReceiptLine &line : receipt.lines) {
        if (line.rule) {
            rows += QStringLiteral("<tr><td colspan=\"2\"><hr/></td></tr>");
            continue;
        }
        QString left = line.left.toHtmlEscaped();
        QString right = line.right.toHtmlEscaped();
        if (line.style != ReceiptLine::Normal) {
            left = QStringLiteral("<b>%1</b>").arg(left);
            right = QStringLiteral("<b>%1</b>").arg(right);
        }
        if (line.style == ReceiptLine::Title || line.style == ReceiptLine::Total) {
            left = QStringLiteral("<span style=\"font-size: 14pt;\">%1</span>").arg(left);
            right = QStringLiteral("<span style=\"font-size: 14pt;\">%1</span>").arg(right);
        }
        if (line.centered || line.style == ReceiptLine::Title)
            rows += QStringLiteral("<tr><td colspan=\"2\" align=\"center\">%1</td></tr>").arg(left);
        else
            rows += QStringLiteral("<tr><td>%1</td><td align=\"right\">%2</td></tr>").arg(left, right);
    }
    return QStringLiteral("<html><body style=\"font-size: 10pt;\"><table width=\"100%\" cellspacing=\"0\""
                          " cellpadding=\"2\">%1</table></body></html>").arg(rows);
}

// --- sending -----------------------------------------------------------------

bool SqlPrintService::sendThermal(const PrinterSettings &settings, const QByteArray &data)
{
    if (settings.name.isEmpty()) {
        m_lastError = tr("Elija la impresora en Preferencias.");
        return false;
    }
    QString error;
    if (m_rawSender(settings.name, data, &error))
        return true;
    m_lastError = error;
    return false;
}

bool SqlPrintService::printOnPage(const PrinterSettings &settings, const Receipt &receipt)
{
    // By name, never the Windows default: it could be anybody's printer
    const QPrinterInfo info = QPrinterInfo::printerInfo(settings.name);
    if (settings.name.isEmpty() || info.isNull()) {
        m_lastError = tr("La impresora %1 no está instalada en este equipo.").arg(settings.name);
        return false;
    }
    QPrinter printer(info, QPrinter::HighResolution);
    QTextDocument document;
    document.setHtml(receiptHtml(receipt));
    document.print(&printer);
    if (printer.printerState() == QPrinter::Error) {
        m_lastError = tr("No se pudo imprimir en %1.").arg(settings.name);
        return false;
    }
    return true;
}

bool SqlPrintService::send(const PrinterSettings &settings, const Receipt &receipt, bool drawer)
{
    switch (settings.kind) {
    case PrinterKind::Thermal:
        return sendThermal(settings, EscPos::render(receipt, settings.columns, settings.cut, drawer));
    case PrinterKind::System:
        return printOnPage(settings, receipt);
    default:
        m_lastError = tr("Este equipo no tiene impresora: se configura en Preferencias.");
        return false;
    }
}

void SqlPrintService::markPrinted(const SaleRecord &sale)
{
    m_sales->recordPrint(sale.id);
    if (sale.printCount > 0 && m_users)
        m_users->audit(QStringLiteral("venta.reimprimir"), QStringLiteral("venta"),
                       AguaraSale::formatNumber(sale.registerId, sale.number));
}

bool SqlPrintService::finishSale(qint64 saleId)
{
    const PrinterSettings settings = printerSettings();
    if (settings.kind == PrinterKind::None)
        return true;
    const std::optional<SaleRecord> sale = m_sales->sale(saleId);
    if (!sale) {
        m_lastError = m_sales->lastError();
        return false;
    }

    bool cash = false;
    for (const SalePayment &p : sale->payments)
        cash = cash || p.method == QLatin1String(PaymentMethod::Cash);
    const bool drawer = cash && settings.openDrawerOnCash && settings.kind == PrinterKind::Thermal;

    if (settings.printEverySale) {
        if (!send(settings, saleReceipt(*sale), drawer))
            return false;
        markPrinted(*sale);
        return true;
    }
    return !drawer || sendThermal(settings, EscPos::drawerPulse());
}

bool SqlPrintService::printSale(qint64 saleId)
{
    const std::optional<SaleRecord> sale = m_sales->sale(saleId);
    if (!sale) {
        m_lastError = m_sales->lastError();
        return false;
    }
    if (sale->state != QLatin1String(SaleState::Paid)) {
        m_lastError = tr("La venta está anulada: no se imprime.");
        return false;
    }
    if (!send(printerSettings(), saleReceipt(*sale), false))
        return false;
    markPrinted(*sale);
    return true;
}

bool SqlPrintService::saveSalePdf(qint64 saleId, const QString &fileName)
{
    const std::optional<SaleRecord> sale = m_sales->sale(saleId);
    if (!sale) {
        m_lastError = m_sales->lastError();
        return false;
    }
    if (sale->state != QLatin1String(SaleState::Paid)) {
        m_lastError = tr("La venta está anulada: no se imprime.");
        return false;
    }
    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(fileName);
    QTextDocument document;
    document.setHtml(receiptHtml(saleReceipt(*sale)));
    document.print(&printer);
    if (printer.printerState() == QPrinter::Error || !QFile::exists(fileName)) {
        m_lastError = tr("No se pudo guardar el archivo.");
        return false;
    }
    markPrinted(*sale);
    return true;
}

bool SqlPrintService::printReceipt(const Receipt &receipt)
{
    return send(printerSettings(), receipt, false);
}

bool SqlPrintService::printTest(const PrinterSettings &settings)
{
    Receipt r;
    r.center(tr("Prueba de impresión"), ReceiptLine::Title);
    r.rule();
    r.add(tr("Impresora"), settings.name);
    r.add(tr("Ancho"), tr("%1 columnas").arg(settings.columns));
    r.add(tr("Acentos"), QStringLiteral("áéíóú ñ Ñ ü ¿?"));
    r.rule();
    r.add(tr("TOTAL Gs."), AguaraMoney::format(123456), ReceiptLine::Total);
    return send(settings, r, false);
}

bool SqlPrintService::openDrawer()
{
    const PrinterSettings settings = printerSettings();
    if (settings.kind != PrinterKind::Thermal) {
        m_lastError = tr("El cajón se abre desde una impresora térmica: configúrela en Preferencias.");
        return false;
    }
    if (!sendThermal(settings, EscPos::drawerPulse()))
        return false;
    if (m_users)
        m_users->audit(QStringLiteral("caja.abrir_cajon"), QStringLiteral("caja"), m_cash->currentRegister().name);
    return true;
}

// --- the Windows queue -------------------------------------------------------

bool SqlPrintService::sendToWindowsQueue(const QString &printer, const QByteArray &data, QString *error)
{
#ifdef Q_OS_WIN
    const auto failed = [&](const char *step) {
        qCWarning(lcApp) << "Printing to" << printer << "failed at" << step << ": Windows error" << GetLastError();
        *error = tr("No se pudo imprimir en %1. Revise que esté encendida y conectada.").arg(printer);
        return false;
    };

    std::wstring name = printer.toStdWString();
    HANDLE handle = nullptr;
    if (!OpenPrinterW(&name[0], &handle, nullptr))
        return failed("OpenPrinter");

    // Windows queues the job for a printer that is off, and it would all come
    // out later: an old ticket, and the drawer opening by itself.
    DWORD needed = 0;
    GetPrinterW(handle, 2, nullptr, 0, &needed);
    QByteArray info(int(needed), Qt::Uninitialized);
    if (needed && GetPrinterW(handle, 2, reinterpret_cast<LPBYTE>(info.data()), needed, &needed)) {
        const auto details = reinterpret_cast<const PRINTER_INFO_2W *>(info.constData());
        if ((details->Attributes & PRINTER_ATTRIBUTE_WORK_OFFLINE) || (details->Status & PRINTER_STATUS_OFFLINE)) {
            ClosePrinter(handle);
            *error = tr("La impresora %1 figura fuera de línea.").arg(printer);
            return false;
        }
    }

    wchar_t documentName[] = L"Ticket AguaraPOS";
    wchar_t dataType[] = L"RAW";
    DOC_INFO_1W document = { documentName, nullptr, dataType };
    if (!StartDocPrinterW(handle, 1, reinterpret_cast<LPBYTE>(&document))) {
        const bool ok = failed("StartDocPrinter");
        ClosePrinter(handle);
        return ok;
    }
    bool ok = StartPagePrinter(handle);
    DWORD written = 0;
    ok = ok && WritePrinter(handle, const_cast<char *>(data.constData()), DWORD(data.size()), &written)
         && written == DWORD(data.size());
    if (!ok)
        failed("WritePrinter");
    EndPagePrinter(handle);
    EndDocPrinter(handle);
    ClosePrinter(handle);
    return ok;
#else
    Q_UNUSED(printer)
    Q_UNUSED(data)
    *error = tr("Imprimir en una térmica solo funciona en Windows.");
    return false;
#endif
}
