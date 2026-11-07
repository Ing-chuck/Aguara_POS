#ifndef PROMOTIONSFACTORY_H
#define PROMOTIONSFACTORY_H

#include <interfaces.h>

#include <QObject>

/// Plugin root object: the page of promotions. The till applies them by itself.
class PromotionsFactory : public QObject,
                          public AguaraModuleInterface,
                          public WidgetModuleInterface,
                          public PermissionProvider
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID AguaraModuleInterface_iid FILE "module.json")
    Q_INTERFACES(AguaraModuleInterface WidgetModuleInterface PermissionProvider)

public:
    explicit PromotionsFactory(QObject *parent = nullptr);

    ModuleInfo moduleInfo() const override;
    bool initialize(AguaraHost *host) override;
    QWidget *createWidget(QWidget *parent = nullptr) override;
    QList<PermissionInfo> permissions() const override;

private:
    PromotionService *promotions = nullptr;
    ProductService *products = nullptr;
    InventoryService *inventory = nullptr;
    UserService *users = nullptr;
};

#endif // PROMOTIONSFACTORY_H
