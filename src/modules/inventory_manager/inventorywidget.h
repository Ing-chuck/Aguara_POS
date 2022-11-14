#ifndef INVENTORYWIDGET_H
#define INVENTORYWIDGET_H

#include <interfaces.h>
#include "inventorysqlmodel.h"

QT_BEGIN_NAMESPACE
namespace Ui { class inventoryWidget; }
class QSqlRelationalTableModel;
class QCompleter;
QT_END_NAMESPACE

class addItemDialog;

class inventoryWidget : public AguaraWidget
{
    Q_OBJECT

public:
    explicit inventoryWidget(QWidget *parent = nullptr);
    ~inventoryWidget();

private:
    Ui::inventoryWidget *ui = nullptr;
    addItemDialog *addDialog = nullptr;
    addItemDialog *editDialog = nullptr;
    InventorySqlModel *model = nullptr;
    QCompleter *searchCompleter;

    void loadTable();
    void hideUnusedColumns();
    bool refreshModel();

private slots:
    void onItemAddedEdited(QString id);
    void onItemRemovePressed();
    void onCompleterAccepted(const QModelIndex &index);
};
#endif // INVENTORYWIDGET_H
