#include "mymodulefactory.h"
#include "mymodule.h"

#include <version.h>

MyModuleFactory::MyModuleFactory(QObject *parent) : QObject(parent)
{
}

ModuleInfo MyModuleFactory::moduleInfo() const
{
    ModuleInfo info;
    info.id          = "template_SI";
    info.name        = "template_SI";
    info.description = "Template for a single-instance module";
    info.version     = VERSION_STRING;
    info.icon        = QIcon(":/template_SI/images/icon.png");
    return info;
}

QWidget *MyModuleFactory::createWidget(QWidget *parent)
{
    return new MyModule(parent);
}
