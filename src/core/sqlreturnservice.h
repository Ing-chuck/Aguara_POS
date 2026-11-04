#ifndef SQLRETURNSERVICE_H
#define SQLRETURNSERVICE_H

#include <returnservice.h>

#include <QObject>
#include <QSqlDatabase>

class ConnectionMonitor;
class QSqlQuery;
class SqlCashService;
class SqlSalesService;
class UserService;

/// ReturnService on the core's database.
class SqlReturnService : public QObject, public ReturnService
{
    Q_OBJECT
    Q_INTERFACES(ReturnService)

public:
    /// `monitor`, `sales` and `cash` must outlive the service.
    SqlReturnService(ConnectionMonitor *monitor, SqlSalesService *sales, SqlCashService *cash, QObject *parent = nullptr);

    void setUsers(UserService *users) { m_users = users; }

    std::optional<ReturnableSale> findSale(const QString &number) const override;
    qint64 processReturn(const ReturnRequest &request) override;
    std::optional<ReturnRecord> returnRecord(qint64 id) const override;
    std::optional<CreditNote> creditNote(const QString &number) const override;
    QString lastError() const override { return m_lastError; }

private:
    QSqlDatabase db() const;
    bool exec(QSqlQuery &q) const;
    std::optional<ReturnableSale> returnable(qint64 saleId) const;
    bool checkRequest(const ReturnRequest &request, const ReturnableSale &sale, Money *amount) const;
    qint64 writeReturn(const ReturnRequest &request, const ReturnableSale &sale, const CashSession &session, Money amount);

    ConnectionMonitor *m_monitor;
    SqlSalesService *m_sales;
    SqlCashService *m_cash;
    QString m_connectionName;
    UserService *m_users = nullptr;
    bool m_inTransaction = false;
    mutable QString m_lastError;
};

#endif // SQLRETURNSERVICE_H
