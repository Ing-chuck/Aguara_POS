#ifndef SQLSALESSERVICE_H
#define SQLSALESSERVICE_H

#include "saleoutbox.h"

#include <salesservice.h>

#include <QObject>
#include <QSqlDatabase>
#include <QTimer>

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
    ~SqlSalesService() override;

    /// Who is selling, and where changes are recorded (may stay null).
    void setUsers(UserService *users) { m_users = users; }

    /// The queue, for tests and for setting its file before the first sale.
    SaleOutbox *outbox() { return &m_outbox; }

    qint64 checkout(const SaleDraft &draft, const QList<SalePayment> &payments) override;
    bool online() const override;
    QList<PaymentMethodInfo> paymentMethods(bool inactive = false) const override;
    bool savePaymentMethod(const PaymentMethodInfo &method) override;
    QList<PendingSale> pendingSales() const override;
    bool flushPending() override;
    QString referenceUsedBy(const QString &reference) const override;
    std::optional<SaleRecord> sale(qint64 id) const override;
    QList<SaleRecord> sessionSales(qint64 sessionId) const override;
    bool voidSale(qint64 id, const QString &reason) override;
    bool recordPrint(qint64 id) override;
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

    /// How pushing a sale ended.
    enum class Push {
        Saved,      ///< the database has it
        NoServer,   ///< the connection failed: it stays in the queue
        Refused     ///< the database said no: see m_lastError
    };

    bool validate(const SaleDraft &draft, const SaleTotals &totals) const;
    bool validateArticles(const SaleDraft &draft) const;
    bool checkMethods(const QList<SalePayment> &payments) const;
    bool settle(const QList<SalePayment> &payments, SaleTotals *totals, QList<SalePayment> *settled) const;
    qint64 nextNumber(qint64 registerId);
    qint64 claimNumber(qint64 registerId, qint64 number);
    Push push(const OutboxEntry &entry, qint64 *saleId);
    Push failure(int generation);
    qint64 localNumber(qint64 registerId);
    std::optional<SaleRecord> queuedSale(qint64 id) const;
    QList<SaleRecord> readSales(const QString &where, const QVariantList &values) const;

    ConnectionMonitor *m_monitor;
    SqlProductService *m_products;
    SqlCashService *m_cash;
    QString m_connectionName;
    UserService *m_users = nullptr;
    bool m_inTransaction = false;
    SaleOutbox m_outbox;
    QTimer m_retry;
    bool m_flushing = false;
    mutable QList<PaymentMethodInfo> m_knownMethods;
    mutable QString m_lastError;
};

#endif // SQLSALESSERVICE_H
