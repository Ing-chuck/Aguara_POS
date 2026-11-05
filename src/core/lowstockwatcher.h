#ifndef LOWSTOCKWATCHER_H
#define LOWSTOCKWATCHER_H

#include <QMap>
#include <QObject>
#include <QString>
#include <QTimer>

class CoreHost;
class SqlInventoryService;
class SqlProductService;
class UserService;

/*
    Keeps the "stock bajo" notification true to the stock, as it changes.

    Every change of stock comes through SqlProductService, from this till or
    from another one, so that's what is watched. A change there is often not
    a change here (most sales leave the list as it was), so this only speaks
    when the list does:

        an article newly low    a notification, even if the last was dismissed
        fewer articles low      its text changes, quietly; dismissed stays so
        none left               it goes away
*/
class LowStockWatcher : public QObject
{
    Q_OBJECT

public:
    /// `users` may be null: then everyone is told. All must outlive this.
    LowStockWatcher(SqlProductService *products, SqlInventoryService *inventory, UserService *users,
                    CoreHost *host, QObject *parent = nullptr);

public slots:
    /// Look now. Also for when someone else signs in: only who may open
    /// Inventario is told.
    void check();

private:
    void forget();

    SqlInventoryService *m_inventory;
    UserService *m_users;
    CoreHost *m_host;
    QMap<QString, QString> m_known;     // code -> description, as last reported
    QTimer m_soon;                      // several changes at once are one look
};

#endif // LOWSTOCKWATCHER_H
