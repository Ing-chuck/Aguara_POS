#ifndef SQLPRINTSERVICE_H
#define SQLPRINTSERVICE_H

#include <interfaces.h>

#include <QObject>
#include <QSqlDatabase>

#include <functional>

class ConnectionMonitor;
class QPrinter;
class QSqlQuery;
class SqlCashService;
class SqlSalesService;

/// PrintService: thermal printers through the Windows queue, other printers
/// and PDF through QPrinter.
class SqlPrintService : public QObject, public PrintService
{
    Q_OBJECT
    Q_INTERFACES(PrintService)

public:
    /// How the bytes of a thermal printer leave the program. Tests replace it:
    /// nothing in a test may reach a real printer.
    using RawSender = std::function<bool(const QString &printer, const QByteArray &data, QString *error)>;

    /// `monitor`, `sales` and `cash` must outlive the service.
    SqlPrintService(ConnectionMonitor *monitor, SqlSalesService *sales, SqlCashService *cash,
                    QObject *parent = nullptr);

    void setUsers(UserService *users) { m_users = users; }
    void setRawSender(const RawSender &sender) { m_rawSender = sender; }

    /// To a Windows printer, in RAW mode: the default RawSender.
    static bool sendToWindowsQueue(const QString &printer, const QByteArray &data, QString *error);

    /// What a sale's ticket says.
    Receipt saleReceipt(const SaleRecord &sale);
    /// The HTML of `receipt`, for QPrinter.
    static QString receiptHtml(const Receipt &receipt);

    StoreInfo storeInfo() const override;
    bool saveStoreInfo(const StoreInfo &store) override;
    PrinterSettings printerSettings() const override;
    void savePrinterSettings(const PrinterSettings &settings) override;
    QStringList printers() const override;
    bool finishSale(qint64 saleId) override;
    bool printSale(qint64 saleId) override;
    bool saveSalePdf(qint64 saleId, const QString &fileName) override;
    bool printReceipt(const Receipt &receipt) override;
    bool printTest(const PrinterSettings &settings) override;
    bool openDrawer() override;
    QString lastError() const override { return m_lastError; }

private:
    QSqlDatabase db() const;
    bool exec(QSqlQuery &q) const;
    bool send(const PrinterSettings &settings, const Receipt &receipt, bool drawer);
    bool sendThermal(const PrinterSettings &settings, const QByteArray &data);
    bool printOnPage(const PrinterSettings &settings, const Receipt &receipt);
    void markPrinted(const SaleRecord &sale);

    ConnectionMonitor *m_monitor;
    SqlSalesService *m_sales;
    SqlCashService *m_cash;
    QString m_connectionName;
    UserService *m_users = nullptr;
    RawSender m_rawSender;
    mutable StoreInfo m_knownStore;     // the ticket of a sale charged during an outage needs it
    mutable QString m_lastError;
};

#endif // SQLPRINTSERVICE_H
