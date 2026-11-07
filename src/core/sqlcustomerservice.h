#ifndef SQLCUSTOMERSERVICE_H
#define SQLCUSTOMERSERVICE_H

#include <customerservice.h>

#include <QObject>
#include <QSqlDatabase>

class ConnectionMonitor;
class QSqlQuery;
class UserService;

/// CustomerService on the core's database.
class SqlCustomerService : public QObject, public CustomerService
{
    Q_OBJECT
    Q_INTERFACES(CustomerService)

public:
    explicit SqlCustomerService(ConnectionMonitor *monitor, QObject *parent = nullptr);

    void setUsers(UserService *users) { m_users = users; }

    QList<Customer> customers(const QString &filter = QString(), bool inactive = false) const override;
    std::optional<Customer> customer(qint64 id) const override;
    std::optional<QList<Customer>> allCustomers() const override;
    qint64 saveCustomer(const Customer &customer) override;
    bool setCustomerActive(qint64 id, bool active) override;
    bool importCustomers(const QList<Customer> &customers, int *saved = nullptr) override;
    QString lastError() const override { return m_lastError; }

signals:
    /// Many customers changed at once, so an open list has to show them.
    void changed();

private:
    QSqlDatabase db() const;
    bool exec(QSqlQuery &q) const;
    void audit(const QString &action, qint64 id, const QString &after);

    ConnectionMonitor *m_monitor;
    QString m_connectionName;
    UserService *m_users = nullptr;
    mutable QString m_lastError;
    bool m_inTransaction = false;
};

#endif // SQLCUSTOMERSERVICE_H
