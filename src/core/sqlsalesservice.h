#ifndef SQLSALESSERVICE_H
#define SQLSALESSERVICE_H

#include <salesservice.h>

#include <QObject>
#include <QSqlDatabase>

class ConnectionMonitor;
class QSqlQuery;
class SqlCashService;
class SqlProductService;
class UserService;

/// SalesService on the core's database.
class SqlSalesService : public QObject, public SalesService
{
    Q_OBJECT
    Q_INTERFACES(SalesService)

public:
    /// `monitor`, `products` and `cash` must outlive the service.
    SqlSalesService(ConnectionMonitor *monitor, SqlProductService *products, SqlCashService *cash,
                    QObject *parent = nullptr);

    /// Who is selling, and where changes are recorded (may stay null).
    void setUsers(UserService *users) { m_users = users; }

    qint64 checkout(const SaleDraft &draft, const QList<SalePayment> &payments) override;
    QString referenceUsedBy(const QString &reference) const override;
    std::optional<SaleRecord> sale(qint64 id) const override;
    QList<SaleRecord> sessionSales(qint64 sessionId) const override;
    bool voidSale(qint64 id, const QString &reason) override;
    qint64 holdSale(const SaleDraft &draft) override;
    QList<HeldSale> heldSales() const override;
    std::optional<SaleDraft> takeHeldSale(qint64 id) override;
    QString lastError() const override { return m_lastError; }

private:
    QSqlDatabase db() const;
    bool exec(QSqlQuery &q) const;
    bool begin();
    bool commit();
    void rollback();
    void audit(const QString &action, const QString &entityId, const QString &after);
    QString userName() const;

    bool validate(const SaleDraft &draft, const SaleTotals &totals) const;
    bool settle(const QList<SalePayment> &payments, SaleTotals *totals, QList<SalePayment> *settled) const;
    qint64 nextNumber(qint64 registerId);
    QList<SaleRecord> readSales(const QString &where, const QVariantList &values) const;

    ConnectionMonitor *m_monitor;
    SqlProductService *m_products;
    SqlCashService *m_cash;
    QString m_connectionName;
    UserService *m_users = nullptr;
    bool m_inTransaction = false;
    mutable QString m_lastError;
};

#endif // SQLSALESSERVICE_H
