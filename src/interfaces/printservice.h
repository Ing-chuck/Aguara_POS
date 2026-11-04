#ifndef PRINTSERVICE_H
#define PRINTSERVICE_H

#include <QList>
#include <QString>

struct ReturnRecord;
#include <QStringList>
#include <QtPlugin>

/// One line of a receipt. Whoever writes it doesn't know the paper: the
/// printer's width decides what wraps.
struct ReceiptLine {
    enum Style {
        Normal,
        Strong,     ///< bold
        Title,      ///< twice the size, centered
        Total       ///< bold and twice as tall
    };
    QString left;
    QString right;              ///< aligned to the right edge
    Style   style = Normal;
    bool    centered = false;
    bool    rule = false;       ///< a line across the paper; the texts are ignored
};

/// What gets printed: on a thermal printer as text, on any other as a page.
struct Receipt {
    QList<ReceiptLine> lines;

    void add(const QString &left, const QString &right = QString(), ReceiptLine::Style style = ReceiptLine::Normal)
    {
        ReceiptLine line;
        line.left = left;
        line.right = right;
        line.style = style;
        lines << line;
    }
    void center(const QString &text, ReceiptLine::Style style = ReceiptLine::Normal)
    {
        ReceiptLine line;
        line.left = text;
        line.style = style;
        line.centered = true;
        lines << line;
    }
    void rule()
    {
        ReceiptLine line;
        line.rule = true;
        lines << line;
    }
};

/// Shown on the tickets, and shared by every register. There is no timbrado:
/// the ticket is internal, and a timbrado belongs to the fiscal documents of
/// phase 8 with their own numbering.
struct StoreInfo {
    QString name;
    QString ruc;
    QString address;
    QString phone;
    QString footer;
};

enum class PrinterKind {
    None,       ///< this computer doesn't print
    Thermal,    ///< ESC/POS sent raw to the Windows queue: fast, cuts, opens the drawer
    System      ///< any other printer, through its Windows driver: no drawer
};

/// How this computer prints. Each computer has its own printer, so this is
/// kept in its aguara.ini and not in the database.
struct PrinterSettings {
    PrinterKind kind = PrinterKind::None;
    QString name;                   ///< the Windows printer
    int  columns = 48;              ///< 48 on 80 mm paper, 32 on 58 mm
    bool cut = true;
    bool printEverySale = true;
    bool openDrawerOnCash = true;
};

/// Tickets and the cash drawer. Provided by the core. Every method returns
/// false, or an empty result, on failure, with the reason in lastError().
/// A failure never undoes a sale: it's already saved.
class PrintService {
public:
    virtual ~PrintService() = default;

    virtual StoreInfo storeInfo() const = 0;
    virtual bool saveStoreInfo(const StoreInfo &store) = 0;

    virtual PrinterSettings printerSettings() const = 0;
    virtual void savePrinterSettings(const PrinterSettings &settings) = 0;
    /// The printers installed in Windows.
    virtual QStringList printers() const = 0;

    /// A sale was just charged: its ticket, and the drawer if cash came in, as
    /// this computer's settings say. Without a printer there's nothing to do.
    virtual bool finishSale(qint64 saleId) = 0;

    /// The sale's ticket. The first one that gets printed is the original;
    /// from then on it says COPIA, and that needs ventas.reimprimir: check it
    /// first. Recorded in the audit log. A voided sale isn't printed.
    virtual bool printSale(qint64 saleId) = 0;
    /// The same ticket as a PDF file.
    virtual bool saveSalePdf(qint64 saleId, const QString &fileName) = 0;

    /// A return just done: its ticket, with the credit note's number if it has
    /// one, and the drawer if cash went out. Always printed: the customer takes it.
    virtual bool finishReturn(const ReturnRecord &record) = 0;

    /// Anything a module wants on paper.
    virtual bool printReceipt(const Receipt &receipt) = 0;

    /// A sample with `settings`, before they are saved.
    virtual bool printTest(const PrinterSettings &settings) = 0;

    /// Open the drawer. Check caja.abrir_cajon first. Recorded in the audit log.
    virtual bool openDrawer() = 0;

    /// Why the last call failed, in Spanish for the user.
    virtual QString lastError() const = 0;
};

#define PrintService_iid "org.AguaraPOS.PrintService/1.1"
Q_DECLARE_INTERFACE(PrintService, PrintService_iid)

#endif // PRINTSERVICE_H
