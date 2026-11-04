#ifndef MODIFYTABLEDIALOG_H
#define MODIFYTABLEDIALOG_H

#include <productservice.h>

#include <QDialog>

QT_BEGIN_NAMESPACE
namespace Ui {
class modifyTableDialog;
}
class QCompleter;
class QStandardItemModel;
QT_END_NAMESPACE

/// Add or remove brands or categories.
class modifyTableDialog : public QDialog
{
    Q_OBJECT

public:
    enum List { Brands, Categories };

    explicit modifyTableDialog(ProductService *products, List list, QWidget *parent);
    ~modifyTableDialog();

signals:
    /// An entry was added or removed.
    void listChanged();

private:
    class QComboBox *parentCombo = nullptr;
    class QPushButton *moveButton = nullptr;
    Ui::modifyTableDialog *ui;
    QCompleter *searchCompleter;
    QStandardItemModel *model;
    ProductService *products;
    List list;

    void reload();
    void buildParentChoice();
    void reloadParents();

private slots:
    void onCompleterAccepted(const QModelIndex &index);
    void onAddItem();
    void onRemoveItem();
};

#endif // MODIFYTABLEDIALOG_H
