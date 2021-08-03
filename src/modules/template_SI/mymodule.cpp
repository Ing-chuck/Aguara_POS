#include "mymodule.h"
#include "ui_mymodule.h"

MyModule::MyModule(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::MyModule)
{
    ui->setupUi(this);
}

MyModule::~MyModule()
{
    delete ui;
}

