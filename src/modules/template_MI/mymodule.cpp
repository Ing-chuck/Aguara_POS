#include "mymodule.h"
#include "ui_mymodule.h"
#include "mymodulefactory.h"

MyModule::MyModule(QWidget *parent)
    : AguaraWidget(parent)
    , ui(new Ui::MyModule)
{
    ui->setupUi(this);

    ModuleMsg msg;
    msg.title = "MyModule - Constructor";
    msg.message = "New instance created";
    msg.priority = MsgPriority::PriorityVeryLow;
    emit notify(msg);
}

MyModule::~MyModule()
{
    ModuleMsg msg;
    msg.title = "MyModule - Destructor";
    msg.message = "Instance destroyed";
    msg.priority = MsgPriority::PriorityVeryLow;
    emit notify(msg);

    delete ui;
}
