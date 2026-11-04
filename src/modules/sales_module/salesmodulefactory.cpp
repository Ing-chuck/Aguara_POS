#include "salesmodulefactory.h"
#include "salesmodule.h"

#include <version.h>

SalesModuleFactory::SalesModuleFactory(QObject *parent) : QObject(parent)
{
}

ModuleInfo SalesModuleFactory::moduleInfo() const
{
    ModuleInfo info;
    info.id          = "sales";
    info.name        = "Ventas";
    info.description = "Registro de ventas";
    info.version     = VERSION_STRING;
    info.iconName    = "cart";      // theme icon (AguaraStyle::icon)
    return info;
}

QList<PermissionInfo> SalesModuleFactory::permissions() const
{
    return {
        { modulePermission(QStringLiteral("sales")), QStringLiteral("Abrir Ventas"), QStringLiteral("Módulos"),
          true, true },     // cashiers too
    };
}

QWidget *SalesModuleFactory::createWidget(QWidget *parent)
{
    return new SalesModule(parent);
}
