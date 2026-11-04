#include "customersfactory.h"
#include "customerspage.h"

#include <version.h>

CustomersFactory::CustomersFactory(QObject *parent) : QObject(parent)
{
}

ModuleInfo CustomersFactory::moduleInfo() const
{
    ModuleInfo info;
    info.id          = "customers";
    info.name        = "Clientes";
    info.description = "Clientes, con su RUC o cédula";
    info.version     = VERSION_STRING;
    info.iconName    = "user";     // theme icon (AguaraStyle::icon)
    return info;
}

bool CustomersFactory::initialize(AguaraHost *host)
{
    customers = host->service<CustomerService>();
    users = host->service<UserService>();
    return customers && users;
}

QList<PermissionInfo> CustomersFactory::permissions() const
{
    const QString group = QStringLiteral("Clientes");
    return {
        { modulePermission(QStringLiteral("customers")), QStringLiteral("Abrir Clientes"), QStringLiteral("Módulos"), true, true },
        { QStringLiteral("clientes.editar"), QStringLiteral("Crear y modificar clientes"), group, true, true },
        { QStringLiteral("clientes.baja"), QStringLiteral("Dar de baja clientes"), group },
    };
}

QWidget *CustomersFactory::createWidget(QWidget *parent)
{
    return new CustomersPage(customers, users, parent);
}
