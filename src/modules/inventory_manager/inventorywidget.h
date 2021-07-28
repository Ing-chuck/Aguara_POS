#ifndef INVENTORYWIDGET_H
#define INVENTORYWIDGET_H

#include <interfaces.h>

#include <QObject>
#include <QtPlugin>
#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui { class inventoryWidget; }
QT_END_NAMESPACE

class inventoryWidget : public QWidget, public AguaraModule
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID aguaraModuleInterface_iid FILE "inventory_module.json")
    Q_INTERFACES(AguaraModule)

public:
    inventoryWidget(QWidget *parent = nullptr);
    ~inventoryWidget();

    bool registerModule(QString version) const override;
    QString getName() const override;
    QIcon getIcon() const override;

private:
    Ui::inventoryWidget *ui;
};
#endif // INVENTORYWIDGET_H
