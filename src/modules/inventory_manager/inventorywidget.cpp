#include "inventorywidget.h"
#include "ui_inventorywidget.h"

#include <version.h>

inventoryWidget::inventoryWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::inventoryWidget)
{
    ui->setupUi(this);
}

inventoryWidget::~inventoryWidget()
{
    delete ui;
}


