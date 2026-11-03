#ifndef ADDITEMDIALOG_H
#define ADDITEMDIALOG_H

#include <productservice.h>

#include <QDialog>

QT_BEGIN_NAMESPACE
namespace Ui {
class addItemDialog;
}
class QComboBox;
QT_END_NAMESPACE

/// Add a new product, or edit an existing one.
class addItemDialog : public QDialog
{
    Q_OBJECT

public:
    explicit addItemDialog(ProductService *products, QWidget *parent = nullptr);
    ~addItemDialog();

signals:
    void itemAddedEdited(QString id);

public slots:
    void addItem();
    void editItem(const QString &code);

private:
    Ui::addItemDialog *ui;
    ProductService *products;

    bool editing = false;
    QString previousCode;       // code of the product being edited

    void reloadLists();
    static void fillNamedCombo(QComboBox *combo, const QList<NamedItem> &items);
    static void selectNamed(QComboBox *combo, qint64 id, const QString &name);
    void showModal();
    void clearForm();
    void updateMargin();
    bool readInput(Product *product);

    void reject() override;
    void accept() override;

private slots:
    void onModifyBrandClicked();
    void onModifyTypeClicked();
};

#endif // ADDITEMDIALOG_H
