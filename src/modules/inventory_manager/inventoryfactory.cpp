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
    info.name        = "Inventory Manager";
    info.description = "Alta, baja y modificacion de articulos";
    info.version     = VERSION_STRING;
    info.icon        = QIcon(":/inventory_manager/images/icon.png");
    return info;
}

bool InventoryFactory::initialize(AguaraHost *host)
{
    products = host->service<ProductService>();
    return products != nullptr;
}

QWidget *InventoryFactory::createWidget(QWidget *parent)
{
    return new inventoryWidget(products, parent);
}
