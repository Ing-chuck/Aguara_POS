#ifndef SALESMODULE_H
#define SALESMODULE_H

#include <interfaces.h>

#include <QHash>

class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QTableWidget;

/// The checkout screen. Everything can be done from the keyboard, and nothing
/// that can happen while scanning opens a dialog: a dialog would swallow the
/// next code.
class SalesModule : public AguaraWidget
{
    Q_OBJECT

public:
    SalesModule(ProductService *products, InventoryService *inventory, UserService *users,
                CashService *cash, SalesService *sales, PrintService *print, CustomerService *customers,
                ReturnService *returns, PromotionService *promotions, QWidget *parent = nullptr);

protected:
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QWidget *buildClosedPage();
    QWidget *buildSalePage();
    QPushButton *addKeyButton(const QString &text, const char *name, void (SalesModule::*action)());
    void buildCashMenu(QPushButton *button);

    void refreshSession();
    void renderSale();
    void refreshConnection();
    bool mayStartSomething();
    void say(const char *role, const QString &text);
    void complain(const QString &text);
    int currentLine() const;

    // ringing up
    void submitEntry();
    void addScan(const ScanResult &scan, bool explicitQuantity, Quantity quantity);
    /// An article with an expired lot: the cashier looks at the unit, and may write
    /// off part of `quantity`, into *discarded. false: a write-off failed.
    bool expiryCheck(const Product &article, Quantity quantity, Quantity *discarded);
    /// The line's best promotion for its quantity today. A cashier's discount
    /// gives way if the two together would pass the line.
    void applyPromotion(SaleLine &line);
    void searchArticle();
    void changeQuantity();
    void removeLine();
    void discountLine();
    void discountSale();
    bool mayDiscount(Money discount, Money base, const QString &what);
    void holdSale();
    void recallSale();
    void cancelSale();
    void charge();
    void chooseCustomer();

    // the register
    void openRegister();
    void closeRegister();
    void cashMovement();
    void openDrawer();
    void returnSale();
    void configureMethods();
    void showSessionSales();
    void showSessions();
    void showPending();
    void renameRegister();

    ProductService *products;
    InventoryService *inventory;
    UserService *users;
    CashService *cash;
    SalesService *sales;
    PrintService *print;
    CustomerService *customers;
    ReturnService *returns;
    PromotionService *promotions;

    SaleDraft draft;
    QHash<QString, QList<Promotion>> offers;    // by article: what an outage still knows
    bool sessionOpen = false;

    QLabel *subtitle = nullptr;
    QPushButton *cashButton = nullptr;
    QStackedWidget *pages = nullptr;
    QLabel *closedTitle = nullptr;
    QLabel *closedText = nullptr;
    QPushButton *closedButton = nullptr;
    QLineEdit *entry = nullptr;
    QTableWidget *table = nullptr;
    QLabel *message = nullptr;
    QLabel *connectionBanner = nullptr;
    QLabel *totalLabel = nullptr;
    QLabel *subtotalLabel = nullptr;
    QLabel *discountLabel = nullptr;
    QLabel *vatLabel = nullptr;
    QLabel *countLabel = nullptr;
    QLabel *customerLabel = nullptr;
    QLabel *lastChange = nullptr;
    QPushButton *recallButton = nullptr;
};
#endif // SALESMODULE_H
