#ifndef INVENTORYWIDGET_H
#define INVENTORYWIDGET_H

#include <interfaces.h>

#include <QObject>
#include <QtPlugin>
#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui { class inventoryWidget; }
QT_END_NAMESPACE

class inventoryWidget : public QObject, public AguaraModule
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID aguaraModuleInterface_iid FILE "inventory_module.json")
    Q_INTERFACES(AguaraModule)

public:
    bool registerModule(QString version) const override;
    //inventoryWidget(QWidget *parent = nullptr);
    //~inventoryWidget();

private:
    Ui::inventoryWidget *ui;
};
#endif // INVENTORYWIDGET_H
