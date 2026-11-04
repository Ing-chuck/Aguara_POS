#include "customerspage.h"

#include <customerdialog.h>

#include <QCheckBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

CustomersPage::CustomersPage(CustomerService *customers, UserService *users, QWidget *parent)
    : AguaraWidget(parent), customers(customers), users(users)
{
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto header = new QWidget;
    AguaraStyle::setRole(header, AguaraStyle::PageHeader);
    header->setAttribute(Qt::WA_StyledBackground);
    auto headerRow = new QHBoxLayout(header);
    headerRow->setContentsMargins(24, 14, 24, 14);
    auto title = new QLabel(tr("Clientes"));
    AguaraStyle::setRole(title, AguaraStyle::Title);
    headerRow->addWidget(title, 1);
    auto addButton = new QPushButton(tr("Nuevo cliente"));
    addButton->setObjectName(QStringLiteral("customerAdd"));
    AguaraStyle::setRole(addButton, AguaraStyle::Primary);
    headerRow->addWidget(addButton);
    layout->addWidget(header);

    auto body = new QVBoxLayout;
    body->setContentsMargins(24, 16, 24, 16);
    body->setSpacing(10);
    auto top = new QHBoxLayout;
    search = new QLineEdit;
    search->setObjectName(QStringLiteral("customerSearch"));
    search->setPlaceholderText(tr("Nombre, RUC o cédula"));
    AguaraStyle::searchField(search);
    top->addWidget(search, 1);
    inactive = new QCheckBox(tr("Ver los dados de baja"));
    inactive->setObjectName(QStringLiteral("customerInactive"));
    top->addWidget(inactive);
    body->addLayout(top);

    table = new QTableWidget(0, 5);
    table->setObjectName(QStringLiteral("customerTable"));
    table->setHorizontalHeaderLabels({ tr("Nombre"), tr("Documento"), tr("Teléfono"), tr("Dirección"), tr("Estado") });
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setAlternatingRowColors(true);
    table->setShowGrid(false);
    table->verticalHeader()->hide();
    table->horizontalHeader()->setHighlightSections(false);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->setColumnWidth(1, 130);
    table->setColumnWidth(2, 120);
    table->setColumnWidth(3, 220);
    body->addWidget(table, 1);

    problem = new QLabel;
    problem->setObjectName(QStringLiteral("customerProblem"));
    problem->setWordWrap(true);
    AguaraStyle::setRole(problem, AguaraStyle::Error);
    problem->hide();
    body->addWidget(problem);

    auto bottom = new QHBoxLayout;
    auto edit = new QPushButton(tr("Modificar"));
    edit->setObjectName(QStringLiteral("customerEdit"));
    bottom->addWidget(edit);
    auto toggle = new QPushButton(tr("Dar de baja o reactivar"));
    toggle->setObjectName(QStringLiteral("customerToggle"));
    bottom->addWidget(toggle);
    bottom->addStretch();
    body->addLayout(bottom);
    layout->addLayout(body, 1);

    connect(search, &QLineEdit::textChanged, this, &CustomersPage::reload);
    connect(inactive, &QCheckBox::toggled, this, &CustomersPage::reload);
    connect(addButton, &QPushButton::clicked, this, &CustomersPage::add);
    connect(edit, &QPushButton::clicked, this, &CustomersPage::editSelected);
    connect(toggle, &QPushButton::clicked, this, &CustomersPage::toggleSelected);
    connect(table, &QTableWidget::cellDoubleClicked, this, &CustomersPage::editSelected);
    reload();
}

void CustomersPage::showEvent(QShowEvent *event)
{
    AguaraWidget::showEvent(event);
    reload();
    search->setFocus();
}

void CustomersPage::reload()
{
    list = customers->customers(search->text(), inactive->isChecked());
    table->setRowCount(list.size());
    for (int row = 0; row < list.size(); ++row) {
        const Customer &c = list.at(row);
        table->setItem(row, 0, new QTableWidgetItem(c.name));
        table->setItem(row, 1, new QTableWidgetItem(c.document));
        table->setItem(row, 2, new QTableWidgetItem(c.phone));
        table->setItem(row, 3, new QTableWidgetItem(c.address));
        table->setItem(row, 4, new QTableWidgetItem(c.active ? tr("Activo") : tr("De baja")));
    }
    if (!list.isEmpty())
        table->selectRow(0);
}

void CustomersPage::add()
{
    if (!users->authorize(QStringLiteral("clientes.editar"), tr("Crear un cliente"), this))
        return;
    if (CustomerDialog(customers, Customer(), this).exec() == QDialog::Accepted)
        reload();
}

void CustomersPage::editSelected()
{
    const int row = table->currentRow();
    if (row < 0 || row >= list.size())
        return;
    if (!users->authorize(QStringLiteral("clientes.editar"), tr("Modificar a %1").arg(list.at(row).name), this))
        return;
    if (CustomerDialog(customers, list.at(row), this).exec() == QDialog::Accepted)
        reload();
}

void CustomersPage::toggleSelected()
{
    const int row = table->currentRow();
    if (row < 0 || row >= list.size())
        return;
    const Customer c = list.at(row);
    if (!users->authorize(QStringLiteral("clientes.baja"), tr("Dar de baja a %1").arg(c.name), this))
        return;
    problem->setVisible(!customers->setCustomerActive(c.id, !c.active));
    problem->setText(customers->lastError());
    reload();
}
