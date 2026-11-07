#ifndef SQLINVENTORYSERVICE_H
#define SQLINVENTORYSERVICE_H

#include <inventoryservice.h>

#include <QMap>
#include <QObject>
#include <QSqlDatabase>

class ConnectionMonitor;
class QSqlQuery;
class SqlProductService;
class UserService;

/// InventoryService on the core's database.
/// A lot with stock, for the notifications.
struct ExpiringLot {
    qint64   id = 0;
    QString  description;
    QDate    expires;
    Quantity quantity = 0;
    int      decimals = 0;
    QString  unit;
};

class SqlInventoryService : public QObject, public InventoryService
{
    Q_OBJECT
    Q_INTERFACES(InventoryService)

public:
    /// `monitor` and `products` must outlive the service.
    SqlInventoryService(ConnectionMonitor *monitor, SqlProductService *products, QObject *parent = nullptr);

    /// Where changes are recorded (may stay null).
    void setAuditLog(UserService *users) { m_audit = users; }

    QList<StockMovement> movements(const QString &code, int limit = 200) const override;
    bool adjustStock(const QString &code, Quantity delta, const QString &reason) override;
    QList<Product> lowStock() const override;
    /// The same, code to description, in one query: it runs after every sale.
    /// Nothing on a failure, which an empty list can't tell from "none low".
    std::optional<QMap<QString, QString>> lowStockNames() const;
    /// Lots of active articles that expire up to `until`, soonest first.
    /// Without a value when the database can't say, as in an outage.
    std::optional<QList<ExpiringLot>> expiringBy(const QDate &until) const;
    int expiryWarningDays() const override;
    bool setExpiryWarningDays(int days) override;
    QList<StockLot> lots(const QString &code) const override;
    bool addLot(const QString &code, Quantity quantity, const QDate &expires) override;
    bool removeLot(qint64 id) override;

    ScanResult resolveScan(const QString &text) const override;
    QStringList barcodes(const QString &code) const override;
    bool addBarcode(const QString &code, const QString &barcode) override;
    bool removeBarcode(const QString &barcode) override;

    QList<Supplier> suppliers(bool includeInactive = false) const override;
    qint64 saveSupplier(const Supplier &supplier) override;
    bool deactivateSupplier(qint64 id) override;
    bool importSuppliers(const QList<Supplier> &suppliers, int *saved = nullptr) override;

    qint64 savePurchase(const Purchase &purchase) override;
    QList<Purchase> purchases(const QDate &from, const QDate &to) const override;
    std::optional<Purchase> purchase(qint64 id) const override;

    QList<CountLine> countSheet(qint64 categoryId) const override;
    bool applyCount(const QList<CountLine> &lines, const QString &note, int *adjusted = nullptr) override;

    QList<PriceChange> priceHistory(const QString &code, int limit = 100) const override;
    QList<PricePreview> previewPriceChange(const PriceFilter &filter, int percent, Money roundTo) const override;
    bool applyPriceChange(const PriceFilter &filter, int percent, Money roundTo, int *changed = nullptr) override;

    QString lastError() const override { return m_lastError; }

signals:
    /// Many suppliers changed at once, so an open list has to show them.
    void suppliersChanged();

private:
    QSqlDatabase db() const;
    bool exec(QSqlQuery &q) const;
    bool begin();
    bool commit();
    void rollback();
    QList<qint64> categoryAndChildren(qint64 id) const;
    void audit(const QString &action, const QString &entityId, const QString &before, const QString &after);

    ConnectionMonitor *m_monitor;
    SqlProductService *m_products;
    UserService *m_audit = nullptr;
    QString m_connectionName;
    mutable QString m_lastError;
    bool m_inTransaction = false;
};

#endif // SQLINVENTORYSERVICE_H
