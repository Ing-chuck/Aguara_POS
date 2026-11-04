#ifndef TILLDIALOGS_H
#define TILLDIALOGS_H

#include <interfaces.h>

#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

/// Pick the sale's customer, or leave it for the final consumer.
class CustomerPickDialog : public QDialog
{
    Q_OBJECT
public:
    CustomerPickDialog(CustomerService *customers, UserService *users, QWidget *parent);

    /// After accepting. Without a customer, id 0: the final consumer.
    Customer customer() const { return picked; }
    void accept() override;

private:
    void reload();
    void add();

    CustomerService *customers;
    UserService *users;
    QList<Customer> list;
    Customer picked;
    QLineEdit *filter;
    QTableWidget *table;
};

/// Return part of a sale. It runs the return itself, and stays open with the
/// reason if the service refuses.
class ReturnDialog : public QDialog
{
    Q_OBJECT
public:
    ReturnDialog(ReturnService *returns, SalesService *sales, const QString &authorizedBy, QWidget *parent);

    /// After accepting.
    qint64 returnId() const { return done; }
    void accept() override;

private:
    void findSale();
    void updateAmount();
    void updateMethod();
    void returnEverything();
    QList<ReturnLine> lines(bool *valid) const;
    void showError(const QString &text);

    ReturnService *returns;
    SalesService *sales;
    QString authorizer;
    std::optional<ReturnableSale> sale;
    qint64 done = 0;
    QLineEdit *number;
    QLabel *saleInfo;
    QTableWidget *table;
    QPushButton *everything;
    QLabel *amount;
    QComboBox *method;
    QLineEdit *reference;
    QLineEdit *reason;
    QLabel *error;
    QPushButton *ok;
};

/// The payment methods: which are in use, and which ask for a code.
class PaymentMethodsDialog : public QDialog
{
    Q_OBJECT
public:
    PaymentMethodsDialog(SalesService *sales, QWidget *parent);
    void accept() override;

private:
    void addRow(const PaymentMethodInfo &method);
    void addNew();

    SalesService *sales;
    QTableWidget *table;
    QLineEdit *newName;
    QLabel *error;
};

#endif // TILLDIALOGS_H
