#include "mymodule.h"
#include "ui_mymodule.h"

#include <QTimer>

/*
**** message notification syntax ****

     ModuleMsg msg("", "id", "title", "text", [priority = normal], [lifetime in seconds = -1]);
     emit notify(msg);

     The core fills in the sender name. Signals emitted directly in the
     constructor are lost (nothing is connected yet), so defer them.
*/

MyModule::MyModule(QWidget *parent)
    : AguaraWidget(parent)
    , ui(new Ui::MyModule)
{
    ui->setupUi(this);

    QTimer::singleShot(0, this, [this]() {
        emit notify(ModuleMsg("", "id", "MyModule - Constructor", "New instance created", MsgPriority::PriorityVeryLow));
    });
}

MyModule::~MyModule()
{
    emit notify(ModuleMsg("", "id", "MyModule - Destructor", "Instance destroyed", MsgPriority::PriorityVeryLow));

    delete ui;
}
