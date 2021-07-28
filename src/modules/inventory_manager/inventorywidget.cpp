#include "inventorywidget.h"
#include "ui_inventorywidget.h"

#include <version.h>

inventoryWidget::inventoryWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::inventoryWidget)
{
    //ui->setupUi(this);
    this->name = "Inventory Manager";
}

inventoryWidget::~inventoryWidget()
{
    delete ui;
}

/// Register the module with the core program
/// compares the core program's version
/// with the module's version to determine compatibility
bool inventoryWidget::registerModule(QString version) const {
    return version == QString(VERSION_STRING);
}

/// Return the name of this module
QString inventoryWidget::getName() const {
    return name;
}

/// Return this module's icon
QIcon inventoryWidget::getIcon() const {
    return QIcon(":/icons/icon.png");
}


