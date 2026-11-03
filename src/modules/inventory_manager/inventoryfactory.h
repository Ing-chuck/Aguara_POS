#ifndef INVENTORYFACTORY_H
#define INVENTORYFACTORY_H

#include <interfaces.h>

#include <QObject>

/// Plugin root object: describes the inventory module and creates its page.
class InventoryFactory : public QObject,
                         public AguaraModuleInterface,
                         public WidgetModuleInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID AguaraModuleInterface_iid FILE "inventory_module.json")
    Q_INTERFACES(AguaraModuleInterface WidgetModuleInterface)

public:
    explicit InventoryFactory(QObject *parent = nullptr);

    ModuleInfo moduleInfo() const override;
    bool initialize(AguaraHost *host) override;
    QWidget *createWidget(QWidget *parent = nullptr) override;

private:
    ProductService *products = nullptr;
};

#endif // INVENTORYFACTORY_H
