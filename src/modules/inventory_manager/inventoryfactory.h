#ifndef INVENTORYFACTORY_H
#define INVENTORYFACTORY_H

#include <interfaces.h>

#include <QObject>

/// Plugin root object: describes the inventory module and creates its page.
class InventoryFactory : public QObject,
                         public AguaraModuleInterface,
                         public WidgetModuleInterface,
                         public PermissionProvider
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID AguaraModuleInterface_iid FILE "inventory_module.json")
    Q_INTERFACES(AguaraModuleInterface WidgetModuleInterface PermissionProvider)

public:
    explicit InventoryFactory(QObject *parent = nullptr);

    ModuleInfo moduleInfo() const override;
    bool initialize(AguaraHost *host) override;
    QWidget *createWidget(QWidget *parent = nullptr) override;
    QList<PermissionInfo> permissions() const override;

private:
    ProductService *products = nullptr;
    InventoryService *inventory = nullptr;
    UserService *users = nullptr;
};

#endif // INVENTORYFACTORY_H
