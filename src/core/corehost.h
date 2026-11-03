#ifndef COREHOST_H
#define COREHOST_H

#include "aguarahost.h"

#include <QByteArray>
#include <QHash>
#include <QObject>

/// The core's implementation of AguaraHost: a registry of services plus a
/// route for module notifications to the main window.
class CoreHost : public QObject, public AguaraHost
{
    Q_OBJECT
public:
    using QObject::QObject;

    /// `service` must implement the interface `iid` (Q_INTERFACES) and
    /// outlive every module.
    void registerService(const char *iid, QObject *service);

    QObject *serviceObject(const char *iid) const override;
    void notify(const ModuleMsg &msg) override;

signals:
    void notificationRequested(const ModuleMsg &msg);

private:
    QHash<QByteArray, QObject *> m_services;
};

#endif // COREHOST_H
