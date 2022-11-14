#include "salesmodule.h"
#include "ui_salesmodule.h"

/*
**** message notification syntax ****

     msg(this->name, "id", "title", "text", [priority = normal], [lifetime in seconds = -1])
     emit notify(msg);
*/


SalesModule::SalesModule(QWidget *parent)
    : AguaraWidget(parent)
    , ui(new Ui::SalesModule)
{
    ui->setupUi(this);
}

SalesModule::~SalesModule()
{
    delete ui;
}

