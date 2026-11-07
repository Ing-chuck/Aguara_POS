#ifndef PROMOTIONDIALOG_H
#define PROMOTIONDIALOG_H

#include <interfaces.h>

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDateEdit;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QStackedWidget;
class QTableView;
class QTableWidget;
class SearchProxyModel;

/// Enter belongs to the fields: a scanner writes here, and a code must not save.
class EnterKeepingDialog : public QDialog
{
    Q_OBJECT
public:
    using QDialog::QDialog;

protected:
    void keyPressEvent(QKeyEvent *event) override;
};

/// A promotion: what it gives, when, and to which articles. The count of
/// articles follows the selection, so a wrong rule shows before saving.
class PromotionDialog : public EnterKeepingDialog
{
    Q_OBJECT
public:
    PromotionDialog(PromotionService *promotions, ProductService *products, InventoryService *inventory,
                    const Promotion &promotion, QWidget *parent);

protected:
    void accept() override;

private:
    QWidget *buildDiscount();
    QWidget *buildDates();
    QWidget *buildSelection();
    void addArticle(bool excluded);
    void addChosen(bool excluded);
    void addFromList();
    void addRule(const PromotionRule &rule);
    void removeRule();
    void kindChanged();
    void selectionChanged();
    void renderRules();
    Promotion collect() const;

    PromotionService *promotions;
    ProductService *products;
    InventoryService *inventory;
    qint64 id;
    bool active;
    QList<PromotionRule> rules;

    QLineEdit *name = nullptr;
    QComboBox *kind = nullptr;
    QLabel *valueLabel = nullptr;
    QLineEdit *value = nullptr;
    QLabel *buyLabel = nullptr;
    QSpinBox *buy = nullptr;
    QLabel *payLabel = nullptr;
    QSpinBox *pay = nullptr;
    QLabel *minimumLabel = nullptr;
    QLineEdit *minimum = nullptr;
    QDateEdit *from = nullptr;
    QDateEdit *to = nullptr;
    QList<QCheckBox *> days;
    QRadioButton *everything = nullptr;
    QRadioButton *chosen = nullptr;
    QCheckBox *inverted = nullptr;
    QCheckBox *categoryAndBrand = nullptr;
    QComboBox *target = nullptr;
    QStackedWidget *targetInput = nullptr;
    QLineEdit *article = nullptr;
    QComboBox *category = nullptr;
    QComboBox *brand = nullptr;
    QPushButton *includeButton = nullptr;
    QTableWidget *ruleTable = nullptr;
    QLabel *reach = nullptr;
    QLabel *problem = nullptr;
};

/// Picks many articles at once from the catalog.
class PromotionPickDialog : public EnterKeepingDialog
{
    Q_OBJECT
public:
    PromotionPickDialog(ProductService *products, QWidget *parent);

    /// The rules for what was marked.
    QList<PromotionRule> rules() const { return picked; }

private:
    void finish(bool exclude);

    QTableView *view;
    SearchProxyModel *proxy;
    QList<PromotionRule> picked;
};

/// The articles a promotion reaches, with their price on it.
class PromotionArticlesDialog : public QDialog
{
    Q_OBJECT
public:
    PromotionArticlesDialog(PromotionService *promotions, const Promotion &promotion, QWidget *parent);
};

#endif // PROMOTIONDIALOG_H
