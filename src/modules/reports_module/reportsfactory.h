#ifndef REPORTSFACTORY_H
#define REPORTSFACTORY_H

#include <interfaces.h>

#include <QObject>

/// Plugin root object: the reports' page.
class ReportsFactory : public QObject,
                       public AguaraModuleInterface,
                       public WidgetModuleInterface,
                       public PermissionProvider
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID AguaraModuleInterface_iid FILE "module.json")
    Q_INTERFACES(AguaraModuleInterface WidgetModuleInterface PermissionProvider)

public:
    explicit ReportsFactory(QObject *parent = nullptr);

    ModuleInfo moduleInfo() const override;
    bool initialize(AguaraHost *host) override;
    QWidget *createWidget(QWidget *parent = nullptr) override;
    QList<PermissionInfo> permissions() const override;

private:
    ReportService *reports = nullptr;
    UserService *users = nullptr;
    SalesService *sales = nullptr;
};

#endif // REPORTSFACTORY_H
