#include "inventorywidget.h"
#include "inventoryfactory.h"
#include "ui_inventorywidget.h"

#include <version.h>

inventoryWidget::inventoryWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::inventoryWidget)
{
    ui->setupUi(this);

    ModuleMsg msg("", "id", "Inventory Manager - Constructor", "New inventory manager instance created", MsgPriority::PriorityVeryLow);
    emitNotification(msg);
}

inventoryWidget::~inventoryWidget()
{
    ModuleMsg msg("", "id", "Inventory Manager - Destructor", "Inventory manager instance destroyed", MsgPriority::PriorityVeryLow);
    emitNotification(msg);

    delete ui;
}

void inventoryWidget::emitNotification(ModuleMsg msg) {
    if(dynamic_cast<InventoryFactory*>(parentWidget())){
        emit dynamic_cast<AmFactory*>(parentWidget())->notify(msg);
    }
}


