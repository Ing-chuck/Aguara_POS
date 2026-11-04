#ifndef STOCKLEDGER_H
#define STOCKLEDGER_H

#include <money.h>
#include <quantity.h>

#include <QString>

class ConnectionMonitor;
class UserService;

/*
    The one place that changes productos.stock.

    A movement and the new balance are written together, inside the caller's
    transaction, so the stock of an article is always the sum of its
    movements. Price and cost changes get the same treatment.
*/
class StockLedger
{
public:
    /// `users` may be null: the records then carry no user name.
    StockLedger(ConnectionMonitor *monitor, QString connectionName, QString *error, UserService *users)
        : m_monitor(monitor), m_connectionName(std::move(connectionName)), m_error(error), m_users(users) {}

    /// Must run inside a transaction. `delta` positive in, negative out.
    bool move(const QString &code, const char *type, Quantity delta, Money unitCost,
              const QString &reason, const QString &document = QString());

    bool recordPrice(const QString &code, Money oldPrice, Money newPrice, Money oldCost, Money newCost,
                     const QString &reason);

private:
    ConnectionMonitor *m_monitor;
    QString m_connectionName;
    QString *m_error;
    UserService *m_users;
};

#endif // STOCKLEDGER_H
