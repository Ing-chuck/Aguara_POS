#include "mymodulefactory.h"
#include "mymodule.h"

#include <version.h>

MyModuleFactory::MyModuleFactory(QObject *parent) : QObject(parent)
{
}

ModuleInfo MyModuleFactory::moduleInfo() const
{
    ModuleInfo info;
    info.id          = "template_MI";
    info.name        = "template_MI";
    info.description = "Template for a multi-instance module";
    info.version     = VERSION_STRING;
    info.icon        = QIcon(":/template_MI/images/icon.png");
    return info;
}

QWidget *MyModuleFactory::createWidget(QWidget *parent)
{
    return new MyModule(parent);
}
