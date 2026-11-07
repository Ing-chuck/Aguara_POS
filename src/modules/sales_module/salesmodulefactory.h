#ifndef SALESMODULEFACTORY_H
#define SALESMODULEFACTORY_H

#include <interfaces.h>

#include <QObject>

/// Plugin root object: describes the sales module and creates its page.
class SalesModuleFactory : public QObject,
                           public AguaraModuleInterface,
                           public WidgetModuleInterface,
                           public PermissionProvider
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID AguaraModuleInterface_iid FILE "module.json")
    Q_INTERFACES(AguaraModuleInterface WidgetModuleInterface PermissionProvider)

public:
    explicit SalesModuleFactory(QObject *parent = nullptr);

    ModuleInfo moduleInfo() const override;
    bool initialize(AguaraHost *host) override;
    QWidget *createWidget(QWidget *parent = nullptr) override;
    bool keepPageOpen() const override;
    QList<PermissionInfo> permissions() const override;

private:
    ProductService *products = nullptr;
    InventoryService *inventory = nullptr;
    UserService *users = nullptr;
    CashService *cash = nullptr;
    SalesService *sales = nullptr;
    PrintService *print = nullptr;
    CustomerService *customers = nullptr;
    ReturnService *returns = nullptr;
    PromotionService *promotions = nullptr;
};

#endif // SALESMODULEFACTORY_H
