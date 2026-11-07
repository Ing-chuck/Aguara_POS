#ifndef PROMOTIONSPAGE_H
#define PROMOTIONSPAGE_H

#include <interfaces.h>

class QCheckBox;
class QLabel;
class QLineEdit;
class QTableWidget;

/// The list of promotions, with what each one is doing today.
class PromotionsPage : public AguaraWidget
{
    Q_OBJECT
public:
    PromotionsPage(PromotionService *promotions, ProductService *products, InventoryService *inventory,
                   UserService *users, QWidget *parent = nullptr);

protected:
    void showEvent(QShowEvent *event) override;

private:
    void reload();
    const Promotion *selected() const;
    void add();
    void editSelected();
    void copySelected();
    void showArticles();
    void toggleSelected();

    PromotionService *promotions;
    ProductService *products;
    InventoryService *inventory;
    UserService *users;
    QList<Promotion> list;
    QLineEdit *search;
    QCheckBox *inactive;
    QTableWidget *table;
    QLabel *problem;
};

#endif // PROMOTIONSPAGE_H
