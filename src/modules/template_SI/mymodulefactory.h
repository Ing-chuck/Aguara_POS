#ifndef MYMODULEFACTORY_H
#define MYMODULEFACTORY_H

#include <interfaces.h>

#include <QObject>

/// Plugin root object: describes the module and creates its pages.
class MyModuleFactory : public QObject,
                        public AguaraModuleInterface,
                        public WidgetModuleInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID AguaraModuleInterface_iid FILE "module.json")
    Q_INTERFACES(AguaraModuleInterface WidgetModuleInterface)

public:
    explicit MyModuleFactory(QObject *parent = nullptr);

    ModuleInfo moduleInfo() const override;
    QWidget *createWidget(QWidget *parent = nullptr) override;
    bool allowsMultipleInstances() const override { return false; }
};

#endif // MYMODULEFACTORY_H
