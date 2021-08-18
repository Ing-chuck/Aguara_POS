#ifndef MYMODULEFACTORY_H
#define MYMODULEFACTORY_H

#include <interfaces.h>

#include <QWidget>
#include <QtPlugin>

class MyModuleFactory : public AmSingleInstanceFactory
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID amSingleInstanceInterface_iid FILE "module.json")
    Q_INTERFACES(AmSingleInstanceFactory)

public:
    explicit MyModuleFactory(QWidget *parent = nullptr);

private:
    QString compiledVersion() const override;
    QSharedPointer<QWidget> makeInstance() override;

};

#endif // MYMODULEFACTORY_H
