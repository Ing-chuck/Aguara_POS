#ifndef INVENTORYPAGES_H
#define INVENTORYPAGES_H

#include <interfaces.h>

#include <QDialog>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QComboBox;
class QDateEdit;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTableWidgetItem;
QT_END_NAMESPACE

/// Suppliers: list, new, edit, deactivate.
class SuppliersPage : public QWidget
{
    Q_OBJECT
public:
    SuppliersPage(InventoryService *inventory, UserService *users, QWidget *parent = nullptr);
    void reload();

private:
    void edit(const Supplier &supplier);

    InventoryService *inventory;
    UserService *users;
    QTableWidget *table;
    QList<Supplier> list;
};

/// Receive merchandise: supplier, invoice, and a line per article.
class PurchaseDialog : public QDialog
{
    Q_OBJECT
public:
    PurchaseDialog(ProductService *products, InventoryService *inventory, QWidget *parent);
    void accept() override;

private:
    void addFromEntry();
    void onCellChanged(int row, int column);
    void updateTotal();
    void updateSuggested(int row);
    Purchase build(bool *ok) const;

    ProductService *products;
    InventoryService *inventory;
    QComboBox *supplier;
    QLineEdit *invoice;
    QDateEdit *date;
    QLineEdit *entry;
    QTableWidget *lines;
    QDoubleSpinBox *margin;
    QCheckBox *updateCosts;
    QLabel *total;
    bool filling = false;
};

/// The purchases received, and the way to load a new one.
class PurchasesPage : public QWidget
{
    Q_OBJECT
public:
    PurchasesPage(ProductService *products, InventoryService *inventory, UserService *users, QWidget *parent = nullptr);
    void reload();

private:
    void showSelected();

    ProductService *products;
    InventoryService *inventory;
    UserService *users;
    QDateEdit *from;
    QDateEdit *to;
    QTableWidget *table;
    QList<Purchase> list;
};

/// Physical count: a sheet per category, filled in by hand or with the scanner.
class CountsPage : public QWidget
{
    Q_OBJECT
public:
    CountsPage(ProductService *products, InventoryService *inventory, UserService *users, QWidget *parent = nullptr);

private:
    void prepare();
    void scan();
    void apply();
    void onCellChanged(int row, int column);
    void updateRow(int row);

    ProductService *products;
    InventoryService *inventory;
    UserService *users;
    QComboBox *category;
    QLineEdit *entry;
    QTableWidget *table;
    QLabel *summary;
    QPushButton *applyButton;
    QList<CountLine> sheet;
    bool filling = false;
};

#endif // INVENTORYPAGES_H
