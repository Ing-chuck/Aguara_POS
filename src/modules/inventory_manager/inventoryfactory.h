#ifndef INVENTORYFACTORY_H
#define INVENTORYFACTORY_H

#include <interfaces.h>

#include <QWidget>
#include <QtPlugin>

class InventoryFactory : public QWidget, public AmSingleInstanceFactory
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID amSingleInstanceInterface_iid FILE "inventory_module.json")
    Q_INTERFACES(AmSingleInstanceFactory)

public:
    explicit InventoryFactory(QWidget *parent = nullptr);

private:
    QString compiledVersion() const override;
    QSharedPointer<QWidget> makeInstance() override;

};

#endif // INVENTORYFACTORY_H
