#ifndef USERSFACTORY_H
#define USERSFACTORY_H

#include <interfaces.h>

#include <QObject>

/// Plugin root object: the administration of users, roles and the audit log.
class UsersFactory : public QObject,
                     public AguaraModuleInterface,
                     public WidgetModuleInterface,
                     public PermissionProvider
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID AguaraModuleInterface_iid FILE "module.json")
    Q_INTERFACES(AguaraModuleInterface WidgetModuleInterface PermissionProvider)

public:
    explicit UsersFactory(QObject *parent = nullptr);

    ModuleInfo moduleInfo() const override;
    bool initialize(AguaraHost *host) override;
    QWidget *createWidget(QWidget *parent = nullptr) override;
    QList<PermissionInfo> permissions() const override;

private:
    UserService *users = nullptr;
};

#endif // USERSFACTORY_H
