#include "salesmodulefactory.h"
#include "salesmodule.h"

#include <version.h>

/// SalesModuleFactory class
/// intantiates a SalesModule when necessary
SalesModuleFactory::SalesModuleFactory(QWidget *parent) : AmSingleInstanceFactory(parent)
{
    this->name = "sales_module";
    this->icon = QIcon(":/sales_module/images/icon.png");
}

/// Program version when this module was compiled
/// used to determine compatibility with the main program
QString SalesModuleFactory::compiledVersion() const {
    return QString(VERSION_STRING);
}

/// Make a new instance of the main module class
QSharedPointer<QWidget> SalesModuleFactory::makeInstance() {
    return QSharedPointer<QWidget>(new SalesModule);
}

