#ifndef CUSTOMERSPAGE_H
#define CUSTOMERSPAGE_H

#include <interfaces.h>

class QCheckBox;
class QLabel;
class QLineEdit;
class QTableWidget;

/// The list of customers, to find, add and edit them.
class CustomersPage : public AguaraWidget
{
    Q_OBJECT
public:
    CustomersPage(CustomerService *customers, UserService *users, QWidget *parent = nullptr);

protected:
    void showEvent(QShowEvent *event) override;

private slots:
    void reload();

private:
    void add();
    void editSelected();
    void toggleSelected();

    CustomerService *customers;
    UserService *users;
    QList<Customer> list;
    QLineEdit *search;
    QCheckBox *inactive;
    QTableWidget *table;
    QLabel *problem;
};

#endif // CUSTOMERSPAGE_H
