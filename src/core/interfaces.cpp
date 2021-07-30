#include "interfaces.h"

/// Register the module with the core program
/// compares the core program's version
/// with the module's compiled version to determine compatibility
bool AguaraModule::registerModule(QString version) const {
    return version == compiledVersion();
}

/// Return the name of this module
QString AguaraModule::getName() const {
    if(name == "")
        qDebug() << "Did you forget to name this module?";
    return name;
}

/// Return this module's icon
QIcon AguaraModule::getIcon() const {
    if(icon.isNull())
        qDebug() << "Did you forget to add an icon to this module?";
    return icon;
}

/// Can this factory create multiple instances of the object?
bool AguaraModule::isMultiInstance() const {
    return multiInstance;
}

/// Setup this factory as single instance
AmSingleInstanceFactory::AmSingleInstanceFactory() {
    this->multiInstance = false;
}

/// Return the created instance
/// make a new one if theres none
QSharedPointer<QWidget> AmSingleInstanceFactory::getInstance() {
    QSharedPointer<QWidget> result(instance);

    if(!this->instance){
        // make a new instance
        result = this->makeInstance();
        instance = result;
    }

    return result;
}

/// Setup this factory as multi instance
AmMultiInstanceFactory::AmMultiInstanceFactory() {
    this->multiInstance = true;
}

/*
/// Return the number of instances
int AmMultiInstanceFactory::getInstanceCount() const {
    return this->instances.count();
}

QList<QSharedPointer<QObject>> AmMultiInstanceFactory::getInstances() const {
    return this->instances;
}
*/

QSharedPointer<QWidget> AmMultiInstanceFactory::getInstance() {
    return this->makeInstance();
}
