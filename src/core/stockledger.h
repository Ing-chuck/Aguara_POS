#ifndef STOCKLEDGER_H
#define STOCKLEDGER_H

#include <money.h>
#include <quantity.h>

#include <QDate>
#include <QString>

#include <optional>

class ConnectionMonitor;
class UserService;

/*
    The one place that changes productos.stock.

    A movement and the new balance are written together, inside the caller's
    transaction, so the stock of an article is always the sum of its
    movements. Price and cost changes get the same treatment.

    Lots follow from here too. A lot is an article and a use-by date, and holds
    only the stock that has one: what remains of the stock has no date, so the
    two can't disagree. A scanner can't tell lots apart, so:

        a sale              leaves the soonest date still good first, then the
                            stock with no date. An expired lot waits for someone
                            to write it off, and only gives way when nothing else
                            is left: the till asked, and that unit wasn't expired
        any other way out   leaves the soonest date first, expired or not
        a dated entry       goes to the lot of that date
        a return            refills the soonest lot that has room: `inicial` is
                            what entered it and wasn't written off, so this undoes
                            what sales took
        any other entry     has no date
*/
class StockLedger
{
public:
    /// `users` may be null: the records then carry no user name.
    StockLedger(ConnectionMonitor *monitor, QString connectionName, QString *error, UserService *users)
        : m_monitor(monitor), m_connectionName(std::move(connectionName)), m_error(error), m_users(users) {}

    /// Must run inside a transaction. `delta` positive in, negative out.
    /// `expires`: the use-by date of what enters.
    bool move(const QString &code, const char *type, Quantity delta, Money unitCost,
              const QString &reason, const QString &document = QString(), const QDate &expires = QDate());

    /// The stock with no date. Without a value on error.
    std::optional<Quantity> undated(const QString &code);
    /// Give `quantity` of it a date. Inside a transaction.
    bool addToLot(const QString &code, const QDate &expires, Quantity quantity);

    bool recordPrice(const QString &code, Money oldPrice, Money newPrice, Money oldCost, Money newCost,
                     const QString &reason);

private:
    std::optional<Quantity> inLots(const QString &code);
    enum class Taken { Sold, WrittenOff };
    bool takeFromLots(const QString &code, Quantity quantity, Taken how, bool includeExpired = true);
    bool refillLots(const QString &code, Quantity quantity);
    bool run(class QSqlQuery &query);

    ConnectionMonitor *m_monitor;
    QString m_connectionName;
    QString *m_error;
    UserService *m_users;
};

#endif // STOCKLEDGER_H
