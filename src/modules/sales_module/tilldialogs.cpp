#include "tilldialogs.h"
#include "saledialogs.h"

#include <customerdialog.h>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

// --- CustomerPickDialog ------------------------------------------------------

CustomerPickDialog::CustomerPickDialog(CustomerService *customers, UserService *users, QWidget *parent)
    : QDialog(parent), customers(customers), users(users)
{
    QVBoxLayout *layout = dialogHeader(this, tr("Cliente de la venta"));
    filter = new QLineEdit;
    filter->setObjectName(QStringLiteral("customerFilter"));
    filter->setPlaceholderText(tr("Nombre, RUC o cédula"));
    AguaraStyle::searchField(filter);
    layout->addWidget(filter);
    table = makeSalesTable({ tr("Nombre"), tr("Documento") });
    table->setObjectName(QStringLiteral("customerPickTable"));
    table->setFocusPolicy(Qt::NoFocus);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->setColumnWidth(1, 140);
    layout->addWidget(table, 1);

    auto row = new QHBoxLayout;
    auto addButton = new QPushButton(tr("Nuevo cliente"));
    addButton->setObjectName(QStringLiteral("customerPickAdd"));
    addButton->setAutoDefault(false);
    row->addWidget(addButton);
    auto finalConsumer = new QPushButton(tr("Consumidor final"));
    finalConsumer->setObjectName(QStringLiteral("customerPickNone"));
    finalConsumer->setAutoDefault(false);
    row->addWidget(finalConsumer);
    row->addStretch();
    row->addWidget(acceptCancel(this, tr("Elegir")));
    layout->addLayout(row);
    resize(600, 420);

    connect(filter, &QLineEdit::textChanged, this, &CustomerPickDialog::reload);
    connect(addButton, &QPushButton::clicked, this, &CustomerPickDialog::add);
    connect(finalConsumer, &QPushButton::clicked, this, [this]() {
        picked = Customer();
        QDialog::accept();
    });
    connect(table, &QTableWidget::cellDoubleClicked, this, &CustomerPickDialog::accept);
    reload();
    filter->setFocus();
}

void CustomerPickDialog::reload()
{
    list = customers->customers(filter->text());
    table->setRowCount(list.size());
    for (int row = 0; row < list.size(); ++row) {
        table->setItem(row, 0, new QTableWidgetItem(list.at(row).name));
        table->setItem(row, 1, new QTableWidgetItem(list.at(row).document));
    }
    if (!list.isEmpty())
        table->selectRow(0);
}

void CustomerPickDialog::add()
{
    if (!users->authorize(QStringLiteral("clientes.editar"), tr("Crear un cliente"), this))
        return;
    Customer fresh;
    fresh.name = filter->text().trimmed();          // what they were looking for
    CustomerDialog dialog(customers, fresh, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    if (const std::optional<Customer> created = customers->customer(dialog.customerId())) {
        picked = *created;
        QDialog::accept();
    }
}

void CustomerPickDialog::accept()
{
    const int row = table->currentRow();
    if (row < 0 || row >= list.size())
        return;
    picked = list.at(row);
    QDialog::accept();
}

// --- ReturnDialog ------------------------------------------------------------

ReturnDialog::ReturnDialog(ReturnService *returns, SalesService *sales, const QString &authorizedBy, QWidget *parent)
    : QDialog(parent), returns(returns), sales(sales), authorizer(authorizedBy)
{
    QVBoxLayout *layout = dialogHeader(this, tr("Devolución"), tr("Escriba el número del ticket y pulse Enter."));
    number = new QLineEdit;
    number->setObjectName(QStringLiteral("returnNumber"));
    number->setPlaceholderText(tr("001-000123"));
    layout->addWidget(number);
    saleInfo = new QLabel;
    saleInfo->setObjectName(QStringLiteral("returnSaleInfo"));
    AguaraStyle::setRole(saleInfo, AguaraStyle::Muted);
    saleInfo->hide();
    layout->addWidget(saleInfo);

    table = makeSalesTable({ tr("Artículo"), tr("Vendido"), tr("Devuelto"), tr("Devolver") });
    table->setObjectName(QStringLiteral("returnLines"));
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int column = 1; column <= 3; ++column)
        table->setColumnWidth(column, 92);
    table->setMinimumHeight(180);
    table->setEditTriggers(QAbstractItemView::AllEditTriggers);
    table->hide();
    layout->addWidget(table, 1);
    everything = new QPushButton(tr("Devolver todo"));
    everything->setObjectName(QStringLiteral("returnAll"));
    everything->setAutoDefault(false);
    everything->hide();
    layout->addWidget(everything, 0, Qt::AlignRight);

    amount = new QLabel;
    amount->setObjectName(QStringLiteral("returnAmount"));
    amount->setStyleSheet(QStringLiteral("font-size: 16pt; font-weight: 600;"));
    amount->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    layout->addWidget(amount);

    auto form = new QFormLayout;
    form->setHorizontalSpacing(12);
    method = new QComboBox;
    method->setObjectName(QStringLiteral("returnMethod"));
    method->setEnabled(false);
    reference = new QLineEdit;
    reference->setObjectName(QStringLiteral("returnReference"));
    reference->setPlaceholderText(tr("Código del comprobante de la reversión"));
    reference->setEnabled(false);
    reason = new QLineEdit;
    reason->setObjectName(QStringLiteral("returnReason"));
    reason->setEnabled(false);
    form->addRow(tr("Devolver por:"), method);
    form->addRow(tr("Comprobante:"), reference);
    form->addRow(tr("Motivo:"), reason);
    layout->addLayout(form);

    error = new QLabel;
    error->setObjectName(QStringLiteral("returnError"));
    error->setWordWrap(true);
    AguaraStyle::setRole(error, AguaraStyle::Error);
    error->hide();
    layout->addWidget(error);

    QDialogButtonBox *buttons = acceptCancel(this, tr("Buscar"));
    ok = buttons->button(QDialogButtonBox::Ok);
    layout->addWidget(buttons);
    resize(700, 640);

    connect(table, &QTableWidget::itemChanged, this, &ReturnDialog::updateAmount);
    connect(everything, &QPushButton::clicked, this, &ReturnDialog::returnEverything);
    connect(method, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ReturnDialog::updateMethod);
    number->setFocus();
}

void ReturnDialog::showError(const QString &text)
{
    error->setText(text);
    error->setVisible(!text.isEmpty());
}

void ReturnDialog::findSale()
{
    sale = returns->findSale(number->text());
    if (!sale) {
        showError(returns->lastError());
        number->selectAll();
        return;
    }
    if (sale->sale.state != QLatin1String(SaleState::Paid)) {
        showError(tr("La venta está anulada."));
        sale.reset();
        return;
    }
    showError(QString());
    number->setEnabled(false);

    QStringList info = { sale->sale.when.toString(QStringLiteral("dd/MM/yyyy HH:mm")),
                         AguaraMoney::formatWithSymbol(sale->sale.totals.total) };
    if (!sale->sale.customerName.isEmpty())
        info << sale->sale.customerName;
    saleInfo->setText(info.join(QStringLiteral(" · ")));
    saleInfo->show();

    table->blockSignals(true);
    table->setRowCount(sale->items.size());
    for (int row = 0; row < sale->items.size(); ++row) {
        const ReturnableItem &item = sale->items.at(row);
        const auto fixed = [](QTableWidgetItem *cell) {
            cell->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            return cell;
        };
        table->setItem(row, 0, fixed(new QTableWidgetItem(item.description)));
        table->setItem(row, 1, fixed(rightItem(AguaraQuantity::format(item.sold, item.decimals))));
        table->setItem(row, 2, fixed(rightItem(AguaraQuantity::format(item.returned, item.decimals))));
        table->setItem(row, 3, rightItem(QString()));
    }
    table->blockSignals(false);
    table->show();
    everything->show();

    method->clear();
    for (const PaymentTotal &p : sale->refundable) {
        if (p.method != QLatin1String(PaymentMethod::CreditNote))
            method->addItem(AguaraSale::methodName(p.method), p.method);
    }
    method->addItem(AguaraSale::methodName(QString::fromLatin1(PaymentMethod::CreditNote)),
                    QString::fromLatin1(PaymentMethod::CreditNote));
    method->setEnabled(true);
    reason->setEnabled(true);
    ok->setText(tr("Devolver"));
    updateMethod();
    updateAmount();
    table->setCurrentCell(0, 3);
    table->setFocus();
}

QList<ReturnLine> ReturnDialog::lines(bool *valid) const
{
    QList<ReturnLine> result;
    *valid = true;
    for (int row = 0; sale && row < table->rowCount(); ++row) {
        const QString text = table->item(row, 3)->text().trimmed();
        if (text.isEmpty())
            continue;
        ReturnLine line;
        line.line = sale->items.at(row).line;
        if (!AguaraQuantity::parse(text, &line.quantity))
            *valid = false;
        result << line;
    }
    return result;
}

void ReturnDialog::updateAmount()
{
    bool valid = false;
    const QList<ReturnLine> list = lines(&valid);
    if (!sale || !valid) {
        amount->clear();
        return;
    }
    const Money exact = AguaraReturn::amount(*sale, list);
    amount->setText(tr("A devolver: %1").arg(AguaraMoney::formatWithSymbol(
        AguaraReturn::paid(method->currentData().toString(), exact))));
}

void ReturnDialog::updateMethod()
{
    const QString code = method->currentData().toString();
    bool needsCode = false;
    for (const PaymentMethodInfo &m : sales->paymentMethods())
        needsCode = needsCode || (m.code == code && m.needsCode);
    reference->setEnabled(needsCode);
    updateAmount();
}

void ReturnDialog::returnEverything()
{
    for (int row = 0; sale && row < table->rowCount(); ++row) {
        const ReturnableItem &item = sale->items.at(row);
        table->item(row, 3)->setText(AguaraQuantity::format(item.sold - item.returned, item.decimals));
    }
}

void ReturnDialog::accept()
{
    if (!sale) {
        findSale();
        return;
    }
    table->setCurrentItem(nullptr);         // ends an edit in progress
    bool valid = false;
    ReturnRequest request;
    request.saleId = sale->sale.id;
    request.lines = lines(&valid);
    request.method = method->currentData().toString();
    request.reference = reference->text();
    request.reason = reason->text();
    request.authorizedBy = authorizer;
    if (!valid) {
        showError(tr("Revise las cantidades a devolver."));
        return;
    }
    done = returns->processReturn(request);
    if (done == 0) {
        showError(returns->lastError());
        return;
    }
    QDialog::accept();
}

// --- PaymentMethodsDialog ----------------------------------------------------

PaymentMethodsDialog::PaymentMethodsDialog(SalesService *sales, QWidget *parent)
    : QDialog(parent), sales(sales)
{
    QVBoxLayout *layout = dialogHeader(this, tr("Medios de pago"),
        tr("El efectivo siempre está. Los demás se concilian aparte, con sus comprobantes en la planilla de cierre."));
    table = makeSalesTable({ tr("Medio"), tr("Pide código"), tr("En uso") });
    table->setObjectName(QStringLiteral("methodsTable"));
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    layout->addWidget(table, 1);
    for (const PaymentMethodInfo &method : sales->paymentMethods(true))
        addRow(method);

    auto row = new QHBoxLayout;
    newName = new QLineEdit;
    newName->setObjectName(QStringLiteral("methodsNew"));
    newName->setPlaceholderText(tr("Nuevo medio: Tigo Money, cheque..."));
    row->addWidget(newName, 1);
    auto add = new QPushButton(tr("Agregar"));
    add->setObjectName(QStringLiteral("methodsAdd"));
    add->setAutoDefault(false);
    row->addWidget(add);
    layout->addLayout(row);

    error = new QLabel;
    error->setObjectName(QStringLiteral("methodsError"));
    error->setWordWrap(true);
    AguaraStyle::setRole(error, AguaraStyle::Error);
    error->hide();
    layout->addWidget(error);
    layout->addWidget(acceptCancel(this, tr("Guardar")));
    resize(520, 420);

    connect(add, &QPushButton::clicked, this, &PaymentMethodsDialog::addNew);
}

void PaymentMethodsDialog::addRow(const PaymentMethodInfo &method)
{
    const int row = table->rowCount();
    table->insertRow(row);
    auto name = new QTableWidgetItem(AguaraSale::methodName(method.code));
    name->setData(Qt::UserRole, method.code);
    table->setItem(row, 0, name);
    const auto check = [](bool on) {
        auto cell = new QTableWidgetItem;
        cell->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        cell->setCheckState(on ? Qt::Checked : Qt::Unchecked);
        return cell;
    };
    table->setItem(row, 1, check(method.needsCode));
    table->setItem(row, 2, check(method.active));
}

void PaymentMethodsDialog::addNew()
{
    PaymentMethodInfo method;
    method.code = newName->text().simplified();
    if (method.code.isEmpty())
        return;
    addRow(method);
    newName->clear();
}

void PaymentMethodsDialog::accept()
{
    for (int row = 0; row < table->rowCount(); ++row) {
        PaymentMethodInfo method;
        method.code = table->item(row, 0)->data(Qt::UserRole).toString();
        method.needsCode = table->item(row, 1)->checkState() == Qt::Checked;
        method.active = table->item(row, 2)->checkState() == Qt::Checked;
        if (!sales->savePaymentMethod(method)) {
            error->setText(sales->lastError());
            error->show();
            return;
        }
    }
    QDialog::accept();
}
