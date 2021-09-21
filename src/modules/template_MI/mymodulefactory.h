#ifndef MYMODULEFACTORY_H
#define MYMODULEFACTORY_H

#include <interfaces.h>

#include <QtPlugin>

class MyModuleFactory : public AmMultiInstanceFactory
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID amMultiInstanceInterface_iid FILE "module.json")
    Q_INTERFACES(AmMultiInstanceFactory)

public:
    explicit MyModuleFactory(QWidget *parent = nullptr);

private:
    QString compiledVersion() const override;
    QSharedPointer<QWidget> makeInstance() override;

};

#endif // MYMODULEFACTORY_H
