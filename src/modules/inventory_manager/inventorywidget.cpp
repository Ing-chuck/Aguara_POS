#include "inventorywidget.h"
#include "inventoryfactory.h"
#include "ui_inventorywidget.h"

#include <version.h>

inventoryWidget::inventoryWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::inventoryWidget)
{
    ui->setupUi(this);

    ModuleMsg msg;
    msg.title = "Inventory Manager - Constructor";
    msg.message = "New inventory manager instance created";
    msg.priority = MsgPriority::PriorityVeryLow;
    emitNotification(msg);
}

inventoryWidget::~inventoryWidget()
{
    ModuleMsg msg;
    msg.title = "Inventory Manager - Destructor";
    msg.message = "Inventory manager instance destroyed";
    msg.priority = MsgPriority::PriorityVeryLow;
    emitNotification(msg);

    delete ui;
}

void inventoryWidget::emitNotification(ModuleMsg msg) {
    if(dynamic_cast<InventoryFactory*>(parentWidget())){
        emit dynamic_cast<AmFactory*>(parentWidget())->notify(msg);
    }
}


