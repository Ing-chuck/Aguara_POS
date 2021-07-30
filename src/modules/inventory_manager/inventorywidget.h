#ifndef INVENTORYWIDGET_H
#define INVENTORYWIDGET_H

#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui { class inventoryWidget; }
QT_END_NAMESPACE

class inventoryWidget : public QWidget
{
    Q_OBJECT

public:
    explicit inventoryWidget(QWidget *parent = nullptr);
    ~inventoryWidget();

private:
    Ui::inventoryWidget *ui;
};
#endif // INVENTORYWIDGET_H
