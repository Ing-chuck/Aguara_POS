#ifndef INVENTORYWIDGET_H
#define INVENTORYWIDGET_H

#include <interfaces.h>

QT_BEGIN_NAMESPACE
namespace Ui { class inventoryWidget; }
class QSqlTableModel;
QT_END_NAMESPACE

class inventoryWidget : public AguaraWidget
{
    Q_OBJECT

public:
    explicit inventoryWidget(QWidget *parent = nullptr);
    ~inventoryWidget();

private:
    Ui::inventoryWidget *ui;

    void loadTable();

    QSqlTableModel *model;
};
#endif // INVENTORYWIDGET_H
