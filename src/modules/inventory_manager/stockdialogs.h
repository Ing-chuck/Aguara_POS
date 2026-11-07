#ifndef STOCKDIALOGS_H
#define STOCKDIALOGS_H

#include <interfaces.h>

#include <QDialog>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QLineEdit;
class QListWidget;
class QRadioButton;
class QSpinBox;
class QTableWidget;
QT_END_NAMESPACE

/// The look of every table in the module.
QTableWidget *makeInventoryTable(const QStringList &headers);
/// An Enter nobody handled no longer presses the dialog's default button. Scanners end every
/// code with Enter, so without this the first scan in a field would save or close the dialog.
void keepEnterInDialog(QDialog *dialog);
/// Wide enough for a whole EAN-13: an elided code can't be checked against the box.
void fitCodeColumn(QTableWidget *table, int column = 0);
/// Takes the value still being typed in a cell, for when saving doesn't move the focus first.
void commitOpenEditor(QTableWidget *table);
/// Enter in a cell sends the focus to `entry`, where the scanner's next code belongs.
void returnFocusOnEnter(QTableWidget *table, QWidget *entry);
/// A title and a subtitle over a dialog.
class QVBoxLayout;
QVBoxLayout *dialogHeader(QDialog *dialog, const QString &title, const QString &subtitle = QString());

/// The signed-in user may, or a supervisor authorizes it on the spot.
bool isAuthorized(UserService *users, const QString &permission, const QString &reason, QWidget *parent);

/// Add or remove units of sale.
class UnitsDialog : public QDialog
{
    Q_OBJECT
public:
    UnitsDialog(ProductService *products, QWidget *parent);

signals:
    /// A unit was added or removed.
    void listChanged();

private:
    void reload();
    void add();
    void remove();

    ProductService *products;
    QTableWidget *table;
    QLineEdit *name;
    QLineEdit *abbreviation;
    QSpinBox *decimals;
    QLabel *problem;
};

/// Correct the stock of one article, with a reason.
class AdjustStockDialog : public QDialog
{
    Q_OBJECT
public:
    AdjustStockDialog(InventoryService *inventory, const Product &product, QWidget *parent);
    void accept() override;

private:
    void updateResult();

    InventoryService *inventory;
    Product product;
    QComboBox *direction;
    QLineEdit *quantity;
    QComboBox *reason;
    QLabel *result;
    QLabel *problem;
};

/// An article's kardex, its lots, its price history and its alternate barcodes.
class ArticleHistoryDialog : public QDialog
{
    Q_OBJECT
public:
    ArticleHistoryDialog(InventoryService *inventory, UserService *users, const Product &product, QWidget *parent);

private:
    void reloadBarcodes();
    void reloadLots();
    QWidget *buildLots();

    InventoryService *inventory;
    UserService *users;
    Product product;
    QListWidget *barcodes;
    QTableWidget *lots;
    QLabel *undated;
};

/// Raise or lower many prices at once, with a preview.
class PriceChangeDialog : public QDialog
{
    Q_OBJECT
public:
    PriceChangeDialog(ProductService *products, InventoryService *inventory, QWidget *parent);
    void accept() override;

private:
    void updatePreview();
    PriceFilter filter() const;
    int percent() const;
    Money roundTo() const;

    InventoryService *inventory;
    QComboBox *category;
    QComboBox *brand;
    QComboBox *supplier;
    QDoubleSpinBox *change;
    QComboBox *rounding;
    QTableWidget *preview;
    QLabel *summary;
    QPushButton *applyButton;
};

/// Print shelf labels for the selected article or a whole category.
class LabelsDialog : public QDialog
{
    Q_OBJECT
public:
    LabelsDialog(ProductService *products, InventoryService *inventory, const QString &selectedCode, QWidget *parent);
    void accept() override;

private:
    ProductService *products;
    InventoryService *inventory;
    QString selectedCode;
    QRadioButton *onlySelected;
    QComboBox *category;
    QSpinBox *copies;
};

/// At the end of an alta: a label for each unit that came in, unless changed.
class EntryLabelsDialog : public QDialog
{
    Q_OBJECT
public:
    EntryLabelsDialog(ProductService *products, const QList<PurchaseLine> &lines, QWidget *parent);
    void accept() override;

private:
    QList<Product> articles;
    QList<QSpinBox *> copies;
    QLabel *problem;
};

#endif // STOCKDIALOGS_H
