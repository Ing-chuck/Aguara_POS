#include "corehost.h"

void CoreHost::registerService(const char *iid, QObject *service)
{
    m_services.insert(QByteArray(iid), service);
}

QObject *CoreHost::serviceObject(const char *iid) const
{
    return m_services.value(QByteArray(iid), nullptr);
}

void CoreHost::notify(const ModuleMsg &msg)
{
    emit notificationRequested(msg);
}

void CoreHost::updateNotification(const ModuleMsg &msg)
{
    emit notificationUpdated(msg);
}

void CoreHost::dismissNotification(const QString &sender, const QString &id)
{
    emit notificationDismissed(sender, id);
}
