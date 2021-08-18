#include "mymodule.h"
#include "ui_mymodule.h"
#include "mymodulefactory.h"

MyModule::MyModule(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::MyModule)
{
    ui->setupUi(this);

    ModuleMsg msg;
    msg.title = "MyModule - Constructor";
    msg.message = "New instance created";
    msg.priority = MsgPriority::PriorityVeryLow;
    emitNotification(msg);
}

MyModule::~MyModule()
{
    ModuleMsg msg;
    msg.title = "MyModule - Destructor";
    msg.message = "Instance destroyed";
    msg.priority = MsgPriority::PriorityVeryLow;
    emitNotification(msg);

    delete ui;
}

void MyModule::emitNotification(ModuleMsg msg) {
    if(dynamic_cast<MyModuleFactory*>(parentWidget())){
        emit dynamic_cast<AmFactory*>(parentWidget())->notify(msg);
    }
}
