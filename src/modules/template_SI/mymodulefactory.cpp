#include "mymodulefactory.h"
#include "mymodule.h"

#include <version.h>

/// MyModuleFactory class
/// intantiates a %{MClass} when necessary
MyModuleFactory::MyModuleFactory(QWidget *parent) : QWidget(parent)
{
    this->name = "template_SI";
    this->icon = QIcon(":/images/icon.png");
}

/// Program version when this module was compiled
/// used to determine compatibility with the main program
QString MyModuleFactory::compiledVersion() const {
    return QString(VERSION_STRING);
}

/// Make a new instance of the main module class
QSharedPointer<QWidget> MyModuleFactory::makeInstance() {
    return QSharedPointer<QWidget>(new MyModule);
}

