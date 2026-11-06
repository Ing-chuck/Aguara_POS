#include "customerdialog.h"
#include "aguarastyle.h"
#include "formcheck.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

CustomerDialog::CustomerDialog(CustomerService *customers, const Customer &customer, QWidget *parent)
    : QDialog(parent), customers(customers), data(customer)
{
    setWindowTitle(customer.id ? tr("Editar cliente") : tr("Nuevo cliente"));
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 18);
    layout->setSpacing(12);
    auto title = new QLabel(windowTitle());
    AguaraStyle::setRole(title, AguaraStyle::Title);
    layout->addWidget(title);

    name = new QLineEdit(customer.name);
    name->setObjectName(QStringLiteral("customerName"));
    type = new QComboBox;
    type->setObjectName(QStringLiteral("customerType"));
    type->addItem(tr("Sin documento"), QString::fromLatin1(DocumentType::None));
    type->addItem(tr("RUC"), QString::fromLatin1(DocumentType::Ruc));
    type->addItem(tr("Cédula de identidad"), QString::fromLatin1(DocumentType::Ci));
    type->setCurrentIndex(qMax(0, type->findData(customer.documentType)));
    document = new QLineEdit(customer.document);
    document->setObjectName(QStringLiteral("customerDocument"));
    document->setPlaceholderText(tr("RUC con su dígito: 80009735-1"));
    address = new QLineEdit(customer.address);
    address->setObjectName(QStringLiteral("customerAddress"));
    phone = new QLineEdit(customer.phone);
    phone->setObjectName(QStringLiteral("customerPhone"));
    email = new QLineEdit(customer.email);
    email->setObjectName(QStringLiteral("customerEmail"));

    auto form = new QFormLayout;
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(10);
    form->addRow(AguaraForm::required(tr("Nombre o razón social:")), name);
    form->addRow(tr("Documento:"), type);
    auto numberLabel = new QLabel(tr("Número:"));
    form->addRow(numberLabel, document);
    form->addRow(tr("Dirección:"), address);
    form->addRow(tr("Teléfono:"), phone);
    form->addRow(tr("Correo:"), email);
    layout->addLayout(form);
    layout->addWidget(AguaraForm::requiredNote());

    error = new QLabel;
    error->setObjectName(QStringLiteral("customerError"));
    error->setWordWrap(true);
    AguaraStyle::setRole(error, AguaraStyle::Error);
    error->hide();
    layout->addWidget(error);

    auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QPushButton *ok = buttons->button(QDialogButtonBox::Ok);
    ok->setText(tr("Guardar"));
    ok->setDefault(true);
    AguaraStyle::setRole(ok, AguaraStyle::Primary);
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancelar"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    setMinimumWidth(460);

    const auto documentRow = [this, numberLabel]() {
        const bool needed = type->currentIndex() != 0;
        document->setEnabled(needed);
        numberLabel->setText(needed ? AguaraForm::required(tr("Número:")) : tr("Número:"));
    };
    connect(type, QOverload<int>::of(&QComboBox::currentIndexChanged), this, documentRow);
    documentRow();
    name->setFocus();
}

void CustomerDialog::accept()
{
    data.name = name->text();
    data.documentType = type->currentData().toString();
    data.document = data.documentType.isEmpty() ? QString() : document->text();
    data.address = address->text();
    data.phone = phone->text();
    data.email = email->text();
    const qint64 id = customers->saveCustomer(data);
    if (id == 0) {
        error->setText(customers->lastError());
        error->show();
        return;
    }
    data.id = id;
    QDialog::accept();
}
