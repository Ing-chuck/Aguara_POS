#ifndef EXPIRYWATCHER_H
#define EXPIRYWATCHER_H

#include <messages.h>

#include <QDate>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

class CoreHost;
class SqlInventoryService;
class SqlProductService;
class UserService;

/*
    Keeps two notifications true to the use-by dates of the lots: what is past
    its date, at the highest level, and what is getting close, as a warning. A
    lot that ran out says nothing: there is nothing to pull from the shelf.

    Like the "stock bajo" one, each speaks only when its list changes:

        a lot newly on it       a notification, even if the last was dismissed
        fewer, or less of one   its text changes, quietly; dismissed stays so
        none left               it goes away

    Time passes with nobody touching a lot, and a till can stay open
    overnight, so the dates are also looked at every hour.
*/
class ExpiryWatcher : public QObject
{
    Q_OBJECT

public:
    /// `users` may be null: then everyone is told. All must outlive this.
    ExpiryWatcher(SqlProductService *products, SqlInventoryService *inventory, UserService *users,
                  CoreHost *host, QObject *parent = nullptr);

    /// check(), as if today were `today`.
    void checkOn(const QDate &today);

public slots:
    /// Look now. Also for when someone else signs in: only who may open
    /// Inventario is told.
    void check();

private:
    struct List {
        QString id;
        MsgPriority priority;
        QMap<qint64, QString> known;    // lot -> what the notification said of it
    };
    /// `found` holds lot -> text, `order` the lots, soonest first.
    void update(List &list, const QMap<qint64, QString> &found, const QList<qint64> &order, const QString &title);
    void forget(List &list);

    SqlInventoryService *m_inventory;
    UserService *m_users;
    CoreHost *m_host;
    List m_expired;
    List m_soon;
    QTimer m_changed;                   // several changes at once are one look
    QTimer m_hourly;
};

#endif // EXPIRYWATCHER_H
