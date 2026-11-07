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
class addItemDialog;
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
    /// The form of a supplier, from here or from an alta. Its id once saved, 0 if cancelled.
    static qint64 showForm(InventoryService *inventory, const Supplier &supplier, QWidget *parent);

public slots:
    void reload();

private:
    QLineEdit *search;
    void edit(const Supplier &supplier);

    InventoryService *inventory;
    UserService *users;
    QTableWidget *table;
    QList<Supplier> list;
};

/// Alta de productos: what arrived, as a sale is typed in. Supplier and invoice,
/// a line per article with its quantity, cost and margin or price, and the way to
/// create on the spot an article that doesn't exist yet.
class PurchaseDialog : public QDialog
{
    Q_OBJECT
public:
    PurchaseDialog(ProductService *products, InventoryService *inventory, UserService *users, QWidget *parent);
    void accept() override;
    /// After accepting: what came in.
    QList<PurchaseLine> received() const { return saved; }

private:
    void addFromEntry();
    void addLine(const Product &product, Quantity quantity);
    void createArticle(const QString &code = QString());
    void createSupplier();
    void fillSuppliers();
    void showMargin(int row);
    void applyMargin(int row);
    void onCellChanged(int row, int column);
    void updateTotal();
    void updateSuggested(int row);
    Purchase build(QString *refused) const;

    ProductService *products;
    InventoryService *inventory;
    UserService *users;
    addItemDialog *newArticle;
    QList<PurchaseLine> saved;
    QComboBox *supplier;
    QLineEdit *invoice;
    QDateEdit *date;
    QLineEdit *entry;
    QTableWidget *lines;
    QDoubleSpinBox *margin;
    QCheckBox *updateCosts;
    QLabel *total;
    QLabel *problem;
    bool filling = false;
};

/// The purchases received, and the way to load a new one.
class PurchasesPage : public QWidget
{
    Q_OBJECT
public:
    PurchasesPage(ProductService *products, InventoryService *inventory, UserService *users, QWidget *parent = nullptr);
    void reload();
    /// Alta de productos, from this page or from the articles' one.
    void receive();

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
