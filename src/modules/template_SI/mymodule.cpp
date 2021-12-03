#include "mymodule.h"
#include "ui_mymodule.h"
#include "mymodulefactory.h"

MyModule::MyModule(QWidget *parent)
    : AguaraWidget(parent)
    , ui(new Ui::MyModule)
{
    ui->setupUi(this);

    ModuleMsg msg(this->name, "id", "MyModule - Constructor", "New instance created", MsgPriority::PriorityVeryLow);
    emit notify(msg);
}

MyModule::~MyModule()
{
    ModuleMsg msg(this->name, "id", "MyModule - Destructor", "Instance destroyed", MsgPriority::PriorityVeryLow);
    emit notify(msg);

    delete ui;
}
