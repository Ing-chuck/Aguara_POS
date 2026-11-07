#include "expirywatcher.h"

#include "corehost.h"
#include "sqlinventoryservice.h"
#include "sqlproductservice.h"

#include <userservice.h>

namespace {

constexpr int kNamesShown = 5;
constexpr int kOneHour = 60 * 60 * 1000;

const QString &watcherName()
{
    static const QString name = QStringLiteral("Inventario");
    return name;
}

} // namespace

ExpiryWatcher::ExpiryWatcher(SqlProductService *products, SqlInventoryService *inventory, UserService *users,
                             CoreHost *host, QObject *parent)
    : QObject(parent), m_inventory(inventory), m_users(users), m_host(host)
{
    m_expired = { QStringLiteral("vencidos"), MsgPriority::PriorityVeryHigh, {} };
    m_soon = { QStringLiteral("por-vencer"), MsgPriority::PriorityHigh, {} };

    m_changed.setSingleShot(true);
    m_changed.setInterval(0);
    connect(&m_changed, &QTimer::timeout, this, &ExpiryWatcher::check);
    connect(products, &SqlProductService::listsRefreshed, &m_changed, QOverload<>::of(&QTimer::start));

    m_hourly.setInterval(kOneHour);
    connect(&m_hourly, &QTimer::timeout, this, &ExpiryWatcher::check);
    m_hourly.start();
}

void ExpiryWatcher::check()
{
    checkOn(QDate::currentDate());
}

void ExpiryWatcher::checkOn(const QDate &today)
{
    if (m_users && !m_users->can(modulePermission(QStringLiteral("inventory")))) {
        forget(m_expired);
        forget(m_soon);
        return;
    }

    const auto lots = m_inventory->expiringBy(today.addDays(m_inventory->expiryWarningDays()));
    if (!lots)
        return;     // no answer, as in an outage, doesn't mean nothing expires

    QMap<qint64, QString> expired, soon;
    QList<qint64> expiredOrder, soonOrder;
    for (const ExpiringLot &lot : *lots) {
        const QString text = QStringLiteral("%1, %2 %3 (%4)").arg(lot.description,
            AguaraQuantity::format(lot.quantity, lot.decimals), lot.unit, lot.expires.toString(QStringLiteral("dd/MM/yyyy")));
        const bool past = lot.expires < today;      // the last day is still good
        (past ? expired : soon).insert(lot.id, text);
        (past ? expiredOrder : soonOrder) << lot.id;
    }
    update(m_expired, expired, expiredOrder,
           expired.size() == 1 ? tr("1 lote vencido") : tr("%1 lotes vencidos").arg(expired.size()));
    update(m_soon, soon, soonOrder,
           soon.size() == 1 ? tr("1 lote por vencer") : tr("%1 lotes por vencer").arg(soon.size()));
}

void ExpiryWatcher::update(List &list, const QMap<qint64, QString> &found, const QList<qint64> &order, const QString &title)
{
    if (found == list.known)
        return;
    if (found.isEmpty()) {
        forget(list);
        return;
    }

    // What's new goes first: with many lots it might not make the list
    QStringList fresh;
    QStringList others;
    for (const qint64 lot : order)
        (list.known.contains(lot) ? others : fresh) << found.value(lot);
    const bool anyNew = !fresh.isEmpty();
    fresh += others;

    const QString names = QStringList(fresh.mid(0, kNamesShown)).join(QStringLiteral(", "))
                          + (found.size() > kNamesShown ? QStringLiteral(", ...") : QString());
    const ModuleMsg message(watcherName(), list.id, title, names, list.priority);
    if (anyNew)
        m_host->notify(message);
    else
        m_host->updateNotification(message);
    list.known = found;
}

void ExpiryWatcher::forget(List &list)
{
    if (list.known.isEmpty())
        return;
    m_host->dismissNotification(watcherName(), list.id);
    list.known.clear();
}
