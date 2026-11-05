#include "lowstockwatcher.h"

#include "corehost.h"
#include "sqlinventoryservice.h"
#include "sqlproductservice.h"

#include <userservice.h>

#include <QStringList>

#include <algorithm>

namespace {

constexpr int kNamesShown = 5;

ModuleMsg stockMessage(const QString &title = QString(), const QString &names = QString())
{
    return ModuleMsg(QStringLiteral("Inventario"), QStringLiteral("stock-bajo"), title, names,
                     MsgPriority::PriorityHigh);
}

} // namespace

LowStockWatcher::LowStockWatcher(SqlProductService *products, SqlInventoryService *inventory, UserService *users,
                                 CoreHost *host, QObject *parent)
    : QObject(parent), m_inventory(inventory), m_users(users), m_host(host)
{
    m_soon.setSingleShot(true);
    m_soon.setInterval(0);
    connect(&m_soon, &QTimer::timeout, this, &LowStockWatcher::check);
    connect(products, &SqlProductService::listsRefreshed, &m_soon, QOverload<>::of(&QTimer::start));
}

void LowStockWatcher::check()
{
    if (m_users && !m_users->can(modulePermission(QStringLiteral("inventory")))) {
        forget();
        return;
    }

    const std::optional<QMap<QString, QString>> found = m_inventory->lowStockNames();
    if (!found)
        return;     // no answer, as in an outage, doesn't mean nothing is low
    const QMap<QString, QString> &low = *found;
    if (low == m_known)
        return;
    if (low.isEmpty()) {
        forget();
        return;
    }

    // What's new goes first: with many articles it might not make the list
    QStringList fresh;
    QStringList others;
    for (auto it = low.cbegin(); it != low.cend(); ++it)
        (m_known.contains(it.key()) ? others : fresh) << it.value();
    std::sort(fresh.begin(), fresh.end());
    std::sort(others.begin(), others.end());
    const bool anyNew = !fresh.isEmpty();
    fresh += others;

    const QString title = low.size() == 1 ? tr("1 artículo con stock bajo")
                                          : tr("%1 artículos con stock bajo").arg(low.size());
    const QString names = QStringList(fresh.mid(0, kNamesShown)).join(QStringLiteral(", "))
                          + (low.size() > kNamesShown ? QStringLiteral(", ...") : QString());
    if (anyNew)
        m_host->notify(stockMessage(title, names));
    else
        m_host->updateNotification(stockMessage(title, names));
    m_known = low;
}

void LowStockWatcher::forget()
{
    if (m_known.isEmpty())
        return;
    const ModuleMsg message = stockMessage();
    m_host->dismissNotification(message.senderName, message.id);
    m_known.clear();
}
