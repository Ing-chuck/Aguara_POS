#include "inventoryfactory.h"
#include "inventorywidget.h"

#include <version.h>

/// InventoryFactory class
/// intantiates a InventoryWidget when necessary
InventoryFactory::InventoryFactory(QWidget *parent) : QWidget(parent), AmSingleInstanceFactory()
{
    this->name = "Inventory Manager";
    this->icon = QIcon(":/inventory_manager/images/icon.png");
}

/// Program version when this module was compiled
/// used to determine compatibility with the main program
QString InventoryFactory::compiledVersion() const {
    return QString(VERSION_STRING);
}

/// Make a new instance of the module class
QSharedPointer<QWidget> InventoryFactory::makeInstance() {
    return QSharedPointer<QWidget>(new inventoryWidget);
}
