#ifndef INVENTORYWIDGET_H
#define INVENTORYWIDGET_H

#include <interfaces.h>

QT_BEGIN_NAMESPACE
namespace Ui { class inventoryWidget; }
class QAbstractItemModel;
class QCompleter;
QT_END_NAMESPACE

class addItemDialog;

class inventoryWidget : public AguaraWidget
{
    Q_OBJECT

public:
    explicit inventoryWidget(ProductService *products, QWidget *parent = nullptr);
    ~inventoryWidget();

private:
    Ui::inventoryWidget *ui = nullptr;
    ProductService *products = nullptr;
    addItemDialog *addDialog = nullptr;
    addItemDialog *editDialog = nullptr;
    QAbstractItemModel *model = nullptr;
    QCompleter *searchCompleter = nullptr;

    void loadTable();
    QString selectedCode() const;

private slots:
    void onItemAddedEdited(QString id);
    void onItemEditPressed();
    void onItemRemovePressed();
    void onCompleterAccepted(const QModelIndex &index);
};
#endif // INVENTORYWIDGET_H
