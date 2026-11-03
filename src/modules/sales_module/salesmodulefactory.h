#ifndef SALESMODULEFACTORY_H
#define SALESMODULEFACTORY_H

#include <interfaces.h>

#include <QObject>

/// Plugin root object: describes the sales module and creates its page.
class SalesModuleFactory : public QObject,
                           public AguaraModuleInterface,
                           public WidgetModuleInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID AguaraModuleInterface_iid FILE "module.json")
    Q_INTERFACES(AguaraModuleInterface WidgetModuleInterface)

public:
    explicit SalesModuleFactory(QObject *parent = nullptr);

    ModuleInfo moduleInfo() const override;
    QWidget *createWidget(QWidget *parent = nullptr) override;
};

#endif // SALESMODULEFACTORY_H
