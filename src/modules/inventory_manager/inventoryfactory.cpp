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
    return products != nullptr;
}

QWidget *InventoryFactory::createWidget(QWidget *parent)
{
    return new inventoryWidget(products, parent);
}
