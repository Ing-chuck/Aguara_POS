#ifndef AGUARAHOST_H
#define AGUARAHOST_H

#include "messages.h"

#include <QObject>

/*
    The host is what the core offers to modules. A module receives it once,
    in AguaraModuleInterface::initialize(), and can keep the pointer: the
    host outlives every module.

    Modules never open the database or know table names. They ask the host
    for a service and work through it:

        bool MyFactory::initialize(AguaraHost *host)
        {
            products = host->service<ProductService>();
            return products != nullptr;    // false: the core skips the module
        }

    New services can be added to the core without changing this interface.
*/
class AguaraHost {
public:
    virtual ~AguaraHost() = default;

    /// Look a service up by interface ID. Prefer the typed service<T>().
    virtual QObject *serviceObject(const char *iid) const = 0;

    /// Show a notification on the core's notifications page.
    /// For modules that have no page (e.g. importers); pages can also use
    /// AguaraWidget::notify().
    virtual void notify(const ModuleMsg &msg) = 0;

    /// Typed lookup: returns nullptr if the core doesn't provide T.
    template <class T>
    T *service() const {
        return qobject_cast<T *>(serviceObject(qobject_interface_iid<T *>()));
    }
};

#endif // AGUARAHOST_H
