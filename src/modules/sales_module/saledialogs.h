#ifndef SALEDIALOGS_H
#define SALEDIALOGS_H

#include <interfaces.h>

#include <QDialog>
#include <QElapsedTimer>

class QAbstractItemModel;
class QDialogButtonBox;
class QGridLayout;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QComboBox;
class QPushButton;
class SearchProxyModel;
class QTableView;
class QTableWidget;
class QTableWidgetItem;
class QVBoxLayout;

// Small pieces shared by the module's dialogs
QVBoxLayout *dialogHeader(QDialog *dialog, const QString &title, const QString &subtitle = QString());
QDialogButtonBox *acceptCancel(QDialog *dialog, const QString &acceptText);
QTableWidget *makeSalesTable(const QStringList &headers);
QTableWidgetItem *rightItem(const QString &text);

/// Yes or no, in the program's own look. true on yes.
bool askConfirm(QWidget *parent, const QString &title, const QString &text, const QString &yesText);
/// A required reason. Empty when cancelled.
QString askReason(QWidget *parent, const QString &title, const QString &text, const QString &placeholder = QString());

/// Change a line's quantity.
class QuantityDialog : public QDialog
{
    Q_OBJECT
public:
    QuantityDialog(const SaleLine &line, QWidget *parent);
    Quantity quantity() const { return value; }
    void accept() override;

private:
    QLineEdit *edit;
    QLabel *error;
    int decimals;
    Quantity value = 0;
};

/// A discount as a percentage or an amount, on `base`.
class DiscountDialog : public QDialog
{
    Q_OBJECT
public:
    DiscountDialog(const QString &title, const QString &what, Money base, QWidget *parent);
    Money discount() const { return value; }
    void accept() override;

private:
    void update();
    bool read(Money *amount) const;

    Money base;
    QComboBox *mode;
    QLineEdit *edit;
    QLabel *result;
    Money value = 0;
};

/// A card or transfer, charged outside the program: its code and its amount.
/// A code already used by another sale needs ventas.comprobante_repetido.
class VoucherDialog : public QDialog
{
    Q_OBJECT
public:
    /// `limit` is what the sale still owes; `taken`, the codes already in it.
    VoucherDialog(const PaymentMethodInfo &method, Money limit, const QStringList &taken,
                  SalesService *sales, UserService *users, ReturnService *returns, QWidget *parent);
    SalePayment payment() const { return result; }
    void accept() override;

private:
    void refuse(const QString &text);
    bool creditNote() const;

    PaymentMethodInfo method;
    Money limit;
    QStringList taken;
    SalesService *sales;
    UserService *users;
    ReturnService *returns;
    QLineEdit *referenceEdit;
    QLineEdit *amountEdit;
    QLabel *error;
    SalePayment result;
};

/// Charge a sale. `net` is the exact amount due. Cards and transfers (F2, F3,
/// F4) go in first; cash pays what they leave, rounded to what cash can pay.
class PaymentDialog : public QDialog
{
    Q_OBJECT
public:
    PaymentDialog(Money net, SalesService *sales, UserService *users, ReturnService *returns, QWidget *parent);
    QList<SalePayment> payments() const;
    bool paidInCash() const { return value > 0; }
    Money change() const { return value - due; }
    void accept() override;
    void reject() override;

protected:
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void addMethodButtons(QGridLayout *grid);
    void addVoucher(const PaymentMethodInfo &method);
    void removeVoucher();
    Money electronic() const;
    void refresh();
    void showChange();

    Money net;
    SalesService *sales;
    UserService *users;
    ReturnService *returns;
    QList<PaymentMethodInfo> methods;
    QList<SalePayment> vouchers;
    Money due;
    QLabel *caption;
    QLabel *totalLabel;
    QLabel *rounded;
    QTableWidget *table;
    QPushButton *removeButton;
    QLineEdit *edit;
    QLabel *changeLabel;
    QLabel *cashError;
    QPushButton *okButton;
    Money value = 0;
    QElapsedTimer sinceLastKey;
    int quickKeys = 0;
    bool scanning = false;
};

/// Find an article by description or code.
/// An article with an expired lot reached the till. Its barcode can't say which
/// lot this unit is from, so the cashier looks at the unit. Scanners end every code
/// with Enter, so neither Enter nor Esc answer: it takes a click, or Tab and Space.
class ExpiryAlertDialog : public QDialog
{
    Q_OBJECT
public:
    /// `expired`: what the lots past their date hold. `inSale`: what this scan
    /// brings. They needn't match, so the cashier says how many go.
    ExpiryAlertDialog(const QString &article, const QDate &date, Quantity expired, Quantity inSale,
                      int decimals, QWidget *parent);
    /// How many to write off; 0 for none.
    Quantity toDiscard() const { return discarded; }

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    void confirmDiscard();

    Quantity inSale;
    int decimals;
    Quantity discarded = 0;
    QLineEdit *count;
    QLabel *problem;
};

class ArticleSearchDialog : public QDialog
{
    Q_OBJECT
public:
    ArticleSearchDialog(ProductService *products, const QString &text, QWidget *parent);
    QString code() const { return selected; }
    void accept() override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QLineEdit *filter;
    QTableView *view;
    SearchProxyModel *proxy;
    QString selected;
};

/// Pick a held sale.
class HeldSalesDialog : public QDialog
{
    Q_OBJECT
public:
    HeldSalesDialog(const QList<HeldSale> &held, QWidget *parent);
    qint64 heldId() const;

private:
    QList<HeldSale> held;
    QTableWidget *table;
};

/// What is still in this computer's queue, and why.
class PendingSalesDialog : public QDialog
{
    Q_OBJECT
public:
    PendingSalesDialog(SalesService *sales, QWidget *parent);

private:
    void reload();

    SalesService *sales;
    QTableWidget *table;
};

/// The sales of the session: voiding, reprinting, saving as PDF.
class SessionSalesDialog : public QDialog
{
    Q_OBJECT
public:
    SessionSalesDialog(SalesService *sales, UserService *users, PrintService *print, qint64 sessionId, QWidget *parent);

private:
    void reload();
    void voidSelected();
    void printSelected();
    void saveSelected();
    bool mayCopy(const SaleRecord &record);
    void showProblem(const QString &text);

    SalesService *sales;
    UserService *users;
    PrintService *print;
    qint64 sessionId;
    QTableWidget *table;
    QList<SaleRecord> list;
};

#endif // SALEDIALOGS_H
