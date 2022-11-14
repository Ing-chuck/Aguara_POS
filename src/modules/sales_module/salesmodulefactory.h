#ifndef SALESMODULEFACTORY_H
#define SALESMODULEFACTORY_H

#include <interfaces.h>

#include <QtPlugin>

class SalesModuleFactory : public AmSingleInstanceFactory
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID amSingleInstanceInterface_iid FILE "module.json")
    Q_INTERFACES(AmSingleInstanceFactory)

public:
    explicit SalesModuleFactory(QWidget *parent = nullptr);

private:
    QString compiledVersion() const override;
    QSharedPointer<QWidget> makeInstance() override;

};

#endif // SALESMODULEFACTORY_H
