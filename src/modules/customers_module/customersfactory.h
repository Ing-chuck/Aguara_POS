#ifndef CUSTOMERSFACTORY_H
#define CUSTOMERSFACTORY_H

#include <interfaces.h>

#include <QObject>

/// Plugin root object: the customers' page.
class CustomersFactory : public QObject,
                         public AguaraModuleInterface,
                         public WidgetModuleInterface,
                         public PermissionProvider
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID AguaraModuleInterface_iid FILE "module.json")
    Q_INTERFACES(AguaraModuleInterface WidgetModuleInterface PermissionProvider)

public:
    explicit CustomersFactory(QObject *parent = nullptr);

    ModuleInfo moduleInfo() const override;
    bool initialize(AguaraHost *host) override;
    QWidget *createWidget(QWidget *parent = nullptr) override;
    QList<PermissionInfo> permissions() const override;

private:
    CustomerService *customers = nullptr;
    UserService *users = nullptr;
};

#endif // CUSTOMERSFACTORY_H
