#include "inventoryfactory.h"
#include "inventorywidget.h"

#include <version.h>

InventoryFactory::InventoryFactory(QObject *parent) : QObject(parent)
{
}

ModuleInfo InventoryFactory::moduleInfo() const
{
    ModuleInfo info;
    info.id          = "inventory";
    info.name        = "Inventario";
    info.description = "Alta, baja y modificación de artículos, precios y stock";
    info.version     = VERSION_STRING;
    info.iconName    = "box";       // theme icon (AguaraStyle::icon)
    return info;
}

bool InventoryFactory::initialize(AguaraHost *host)
{
    products = host->service<ProductService>();
    inventory = host->service<InventoryService>();
    users = host->service<UserService>();
    return products != nullptr && inventory != nullptr;
}

QList<PermissionInfo> InventoryFactory::permissions() const
{
    const QString group = QStringLiteral("Inventario");
    return {
        { QStringLiteral("inventario.editar"), QStringLiteral("Crear y modificar artículos y precios"), group },
        { QStringLiteral("inventario.baja"), QStringLiteral("Dar de baja artículos"), group },
        { QStringLiteral("inventario.ajustar"), QStringLiteral("Ajustar el stock"), group },
        { QStringLiteral("inventario.compras"), QStringLiteral("Cargar compras"), group },
        { QStringLiteral("inventario.recuento"), QStringLiteral("Aplicar recuentos"), group },
        { QStringLiteral("inventario.precios"), QStringLiteral("Cambiar precios en masa"), group },
        { QStringLiteral("inventario.proveedores"), QStringLiteral("Administrar proveedores"), group },
    };
}

QWidget *InventoryFactory::createWidget(QWidget *parent)
{
    return new inventoryWidget(products, inventory, users, parent);
}
