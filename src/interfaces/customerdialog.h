#ifndef CUSTOMERDIALOG_H
#define CUSTOMERDIALOG_H

#include "aguara_global.h"
#include "customerservice.h"

#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;

/// A customer's data. Shared by the customers' page and the till. It saves
/// them itself, and stays open with the reason if the service refuses.
class AGUARA_EXPORT CustomerDialog : public QDialog
{
    Q_OBJECT
public:
    CustomerDialog(CustomerService *customers, const Customer &customer, QWidget *parent);

    /// After accepting.
    qint64 customerId() const { return data.id; }

    void accept() override;

private:
    CustomerService *customers;
    Customer data;
    QLineEdit *name;
    QComboBox *type;
    QLineEdit *document;
    QLineEdit *address;
    QLineEdit *phone;
    QLineEdit *email;
    QLabel *error;
};

#endif // CUSTOMERDIALOG_H
