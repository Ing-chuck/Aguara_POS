#include "inventorywidget.h"
#include "ui_inventorywidget.h"

#include <version.h>
/*
inventoryWidget::inventoryWidget(QWidget *parent)
    : QObject(parent)
    , ui(new Ui::inventoryWidget)
{
    //ui->setupUi(this);
}

inventoryWidget::~inventoryWidget()
{
    delete ui;
}
*/
/// Register the module with the core program
/// compares the core program's version
/// with the module's version to determine compatibility
bool inventoryWidget::registerModule(QString version) const {
    return version == QString(VERSION_STRING);
}

