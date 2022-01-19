#ifndef ADDITEMDIALOG_H
#define ADDITEMDIALOG_H

#include <QDialog>
#include <QPointer>

QT_BEGIN_NAMESPACE
namespace Ui {
class addItemDialog;
}
class QSqlQueryModel;
class QSqlTableModel;
class QTableView;
QT_END_NAMESPACE

class modifyTableDialog;

class addItemDialog : public QDialog
{
    Q_OBJECT

public:
    explicit addItemDialog(QWidget *parent = nullptr);
    ~addItemDialog();

signals:
    void itemAddedEdited(QString id);

public slots:
    void addItem();
    void editItem(QTableView *view);


private:
    Ui::addItemDialog *ui;
    QSqlQueryModel *brandModel = nullptr;
    QSqlQueryModel *typeModel = nullptr;
    QSqlQueryModel *vatModel = nullptr;
    QPointer<modifyTableDialog> modifyDialog;

    // Variables used for editing
    bool editing;
    QSqlTableModel *tableModel = nullptr;
    int selectedRow;

    // Data of item to be added
    QString itemCode;
    QString itemDescription;
    QString itemBrand;
    QString itemType;
    QString itemPrice;
    QString itemCost;
    QString itemStock;
    QString itemStockMin;
    QString itemVat;
    QString itemBuyDate;
    QString itemModDate;

    // In case of item edit
    QString prevCode;

    void getDataFromSelection();
    void getDataFromInput();
    bool checkItemCodeChanged();
    //bool eventFilter(QObject *, QEvent*) override;
    void reject() override;
    void accept() override;


private slots:
    void onModifyBrandClicked();
    void onModifyTypeClicked();
    void onSaveClicked();
};

#endif // ADDITEMDIALOG_H
