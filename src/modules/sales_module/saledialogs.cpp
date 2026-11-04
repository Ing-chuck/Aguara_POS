#include "saledialogs.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QTableWidget>
#include <QVBoxLayout>

QVBoxLayout *dialogHeader(QDialog *dialog, const QString &title, const QString &subtitle)
{
    auto layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(24, 20, 24, 18);
    layout->setSpacing(12);
    dialog->setWindowTitle(title);
    auto heading = new QLabel(title);
    AguaraStyle::setRole(heading, AguaraStyle::Title);
    layout->addWidget(heading);
    if (!subtitle.isEmpty()) {
        auto sub = new QLabel(subtitle);
        sub->setWordWrap(true);
        AguaraStyle::setRole(sub, AguaraStyle::Subtitle);
        layout->addWidget(sub);
    }
    return layout;
}

QDialogButtonBox *acceptCancel(QDialog *dialog, const QString &acceptText)
{
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QPushButton *ok = buttons->button(QDialogButtonBox::Ok);
    ok->setText(acceptText);
    ok->setDefault(true);
    AguaraStyle::setRole(ok, AguaraStyle::Primary);
    buttons->button(QDialogButtonBox::Cancel)->setText(QObject::tr("Cancelar"));
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    return buttons;
}

QTableWidget *makeSalesTable(const QStringList &headers)
{
    auto table = new QTableWidget(0, headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setAlternatingRowColors(true);
    table->setShowGrid(false);
    table->setWordWrap(false);
    table->verticalHeader()->hide();
    table->verticalHeader()->setDefaultSectionSize(34);
    table->horizontalHeader()->setHighlightSections(false);
    table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    return table;
}

QTableWidgetItem *rightItem(const QString &text)
{
    auto item = new QTableWidgetItem(text);
    item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return item;
}

bool askConfirm(QWidget *parent, const QString &title, const QString &text, const QString &yesText)
{
    QDialog dialog(parent);
    QVBoxLayout *layout = dialogHeader(&dialog, title, text);
    layout->addWidget(acceptCancel(&dialog, yesText));
    dialog.setMinimumWidth(380);
    return dialog.exec() == QDialog::Accepted;
}

QString askReason(QWidget *parent, const QString &title, const QString &text)
{
    QDialog dialog(parent);
    QVBoxLayout *layout = dialogHeader(&dialog, title, text);
    auto edit = new QLineEdit;
    edit->setObjectName(QStringLiteral("reasonEdit"));
    edit->setPlaceholderText(QObject::tr("Motivo"));
    layout->addWidget(edit);
    QDialogButtonBox *buttons = acceptCancel(&dialog, QObject::tr("Aceptar"));
    QPushButton *ok = buttons->button(QDialogButtonBox::Ok);
    ok->setEnabled(false);
    QObject::connect(edit, &QLineEdit::textChanged, ok, [ok](const QString &t) { ok->setEnabled(!t.trimmed().isEmpty()); });
    layout->addWidget(buttons);
    dialog.setMinimumWidth(380);
    return dialog.exec() == QDialog::Accepted ? edit->text().trimmed() : QString();
}

// --- QuantityDialog ----------------------------------------------------------

QuantityDialog::QuantityDialog(const SaleLine &line, QWidget *parent)
    : QDialog(parent), decimals(line.decimals)
{
    QVBoxLayout *layout = dialogHeader(this, tr("Cantidad"), line.description);
    edit = new QLineEdit(AguaraQuantity::format(line.quantity, line.decimals));
    edit->setObjectName(QStringLiteral("quantityEdit"));
    edit->selectAll();
    layout->addWidget(edit);
    error = new QLabel;
    AguaraStyle::setRole(error, AguaraStyle::Error);
    error->hide();
    layout->addWidget(error);
    layout->addWidget(acceptCancel(this, tr("Aceptar")));
    setMinimumWidth(340);
}

void QuantityDialog::accept()
{
    Quantity q = 0;
    if (!AguaraQuantity::parse(edit->text(), &q) || q <= 0) {
        error->setText(tr("Escriba una cantidad mayor que cero."));
        error->show();
        return;
    }
    if (decimals == 0 && q % AguaraQuantity::One != 0) {
        error->setText(tr("Este artículo se vende por unidad."));
        error->show();
        return;
    }
    value = q;
    QDialog::accept();
}

// --- DiscountDialog ----------------------------------------------------------

DiscountDialog::DiscountDialog(const QString &title, const QString &what, Money base, QWidget *parent)
    : QDialog(parent), base(base)
{
    QVBoxLayout *layout = dialogHeader(this, title, what);
    auto row = new QHBoxLayout;
    row->setSpacing(8);
    mode = new QComboBox;
    mode->setObjectName(QStringLiteral("discountMode"));
    mode->addItem(tr("Porcentaje"));
    mode->addItem(tr("Monto en Gs."));
    edit = new QLineEdit;
    edit->setObjectName(QStringLiteral("discountEdit"));
    row->addWidget(mode);
    row->addWidget(edit, 1);
    layout->addLayout(row);
    result = new QLabel;
    result->setObjectName(QStringLiteral("discountResult"));
    AguaraStyle::setRole(result, AguaraStyle::Muted);
    layout->addWidget(result);
    layout->addWidget(acceptCancel(this, tr("Aplicar")));
    setMinimumWidth(380);

    connect(edit, &QLineEdit::textChanged, this, &DiscountDialog::update);
    connect(mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &DiscountDialog::update);
    update();
    edit->setFocus();
}

bool DiscountDialog::read(Money *amount) const
{
    const QString text = edit->text().trimmed();
    if (text.isEmpty()) {
        *amount = 0;                // an empty field removes the discount
        return true;
    }
    if (mode->currentIndex() == 1)
        return AguaraMoney::parse(text, amount) && *amount >= 0 && *amount <= base;

    Quantity percent = 0;           // thousandths of a percent: 2,5 % is 2500
    if (!AguaraQuantity::parse(text, &percent) || percent < 0 || percent > AguaraQuantity::whole(100))
        return false;
    *amount = (base * percent + 50000) / 100000;
    return true;
}

void DiscountDialog::update()
{
    Money amount = 0;
    result->setText(read(&amount)
        ? tr("Descuenta Gs. %1, queda Gs. %2").arg(AguaraMoney::format(amount), AguaraMoney::format(base - amount))
        : tr("Revise el valor: no puede superar Gs. %1 ni el 100 %.").arg(AguaraMoney::format(base)));
}

void DiscountDialog::accept()
{
    if (read(&value))
        QDialog::accept();
}

// --- PaymentDialog -----------------------------------------------------------

PaymentDialog::PaymentDialog(Money net, SalesService *sales, UserService *users, ReturnService *returns, QWidget *parent)
    : QDialog(parent), net(net), sales(sales), users(users), returns(returns), due(AguaraSale::cashDue(net, 0))
{
    QVBoxLayout *layout = dialogHeader(this, tr("Cobrar"), tr("Sin monto, se cobra en efectivo lo que falta."));

    caption = new QLabel;
    caption->setObjectName(QStringLiteral("paymentCaption"));
    caption->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    AguaraStyle::setRole(caption, AguaraStyle::Muted);
    layout->addWidget(caption);
    totalLabel = new QLabel;
    totalLabel->setObjectName(QStringLiteral("paymentTotal"));
    totalLabel->setStyleSheet(QStringLiteral("font-size: 26pt; font-weight: 600;"));
    totalLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    layout->addWidget(totalLabel);
    rounded = new QLabel;
    rounded->setObjectName(QStringLiteral("paymentRounding"));
    rounded->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    AguaraStyle::setRole(rounded, AguaraStyle::Muted);
    layout->addWidget(rounded);

    auto grid = new QGridLayout;
    grid->setSpacing(8);
    addMethodButtons(grid);
    layout->addLayout(grid);

    table = makeSalesTable({ tr("Medio"), tr("Comprobante"), tr("Monto") });
    table->setObjectName(QStringLiteral("paymentVouchers"));
    table->setFocusPolicy(Qt::NoFocus);
    table->setMaximumHeight(130);
    table->setColumnWidth(0, 160);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    layout->addWidget(table);
    removeButton = new QPushButton(tr("F5  Quitar comprobante"));
    removeButton->setObjectName(QStringLiteral("paymentRemove"));
    removeButton->setAutoDefault(false);
    connect(removeButton, &QPushButton::clicked, this, &PaymentDialog::removeVoucher);
    layout->addWidget(removeButton, 0, Qt::AlignRight);

    auto form = new QFormLayout;
    form->setHorizontalSpacing(12);
    edit = new QLineEdit;
    edit->setObjectName(QStringLiteral("paymentTendered"));
    edit->setAlignment(Qt::AlignRight);
    form->addRow(tr("Efectivo recibido:"), edit);
    layout->addLayout(form);

    changeLabel = new QLabel;
    changeLabel->setObjectName(QStringLiteral("paymentChange"));
    changeLabel->setStyleSheet(QStringLiteral("font-size: 16pt; font-weight: 600;"));
    changeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    layout->addWidget(changeLabel);
    QDialogButtonBox *buttons = acceptCancel(this, tr("Cobrar"));
    okButton = buttons->button(QDialogButtonBox::Ok);
    layout->addWidget(buttons);
    setMinimumWidth(460);

    connect(edit, &QLineEdit::textChanged, this, &PaymentDialog::showChange);
    refresh();
}

namespace {

/// F5 removes a voucher.
const struct { int key; const char *label; } kMethodKeys[] = {
    { Qt::Key_F2, "F2" }, { Qt::Key_F3, "F3" }, { Qt::Key_F4, "F4" }, { Qt::Key_F6, "F6" },
    { Qt::Key_F7, "F7" }, { Qt::Key_F8, "F8" }, { Qt::Key_F9, "F9" }, { Qt::Key_F10, "F10" },
};
const int kMethodKeyCount = int(sizeof(kMethodKeys) / sizeof(kMethodKeys[0]));

} // namespace

void PaymentDialog::addMethodButtons(QGridLayout *grid)
{
    methods = sales->paymentMethods();
    if (sales->online()) {                  // its balance is on the server
        PaymentMethodInfo note;
        note.code = QString::fromLatin1(PaymentMethod::CreditNote);
        methods << note;
    }
    methods = methods.mid(0, kMethodKeyCount);
    for (int i = 0; i < methods.size(); ++i) {
        const PaymentMethodInfo method = methods.at(i);
        auto button = new QPushButton(QStringLiteral("%1  %2").arg(QLatin1String(kMethodKeys[i].label),
                                                                  AguaraSale::methodName(method.code)));
        button->setObjectName(QStringLiteral("paymentMethod_") + method.code);
        button->setAutoDefault(false);
        connect(button, &QPushButton::clicked, this, [this, method]() { addVoucher(method); });
        grid->addWidget(button, i / 2, i % 2);
    }
}

Money PaymentDialog::electronic() const
{
    Money sum = 0;
    for (const SalePayment &p : vouchers)
        sum += p.amount;
    return sum;
}

void PaymentDialog::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_F5) {
        removeVoucher();
        return;
    }
    for (int i = 0; i < methods.size(); ++i) {
        if (event->key() == kMethodKeys[i].key) {
            addVoucher(methods.at(i));
            return;
        }
    }
    QDialog::keyPressEvent(event);
}

void PaymentDialog::addVoucher(const PaymentMethodInfo &method)
{
    const Money left = net - electronic();
    if (left <= 0)
        return;
    QStringList taken;
    for (const SalePayment &p : vouchers) {
        if (!p.reference.isEmpty())
            taken << p.reference;
    }
    VoucherDialog dialog(method, left, taken, sales, users, returns, this);
    if (dialog.exec() == QDialog::Accepted) {
        vouchers << dialog.payment();
        refresh();
    }
}

void PaymentDialog::removeVoucher()
{
    if (vouchers.isEmpty())
        return;
    const int row = table->currentRow();
    vouchers.removeAt(row >= 0 && row < vouchers.size() ? row : vouchers.size() - 1);
    refresh();
}

/// What is left in cash, after every change to the list of vouchers.
void PaymentDialog::refresh()
{
    const Money paid = electronic();
    const Money left = net - paid;
    due = AguaraSale::cashDue(net, paid);

    caption->setText(paid == 0 ? tr("Total") : due > 0 ? tr("Falta pagar en efectivo") : tr("Pago completo"));
    totalLabel->setText(AguaraMoney::formatWithSymbol(due));
    rounded->setVisible(due != left);
    rounded->setText(tr("%1 exacto %2, redondeado para pagar en efectivo")
                         .arg(paid == 0 ? tr("Total") : tr("Saldo"), AguaraMoney::formatWithSymbol(left)));

    table->setRowCount(vouchers.size());
    for (int row = 0; row < vouchers.size(); ++row) {
        const SalePayment &p = vouchers.at(row);
        table->setItem(row, 0, new QTableWidgetItem(AguaraSale::methodName(p.method)));
        table->setItem(row, 1, new QTableWidgetItem(p.reference));
        table->setItem(row, 2, rightItem(AguaraMoney::format(p.amount)));
    }
    table->setVisible(!vouchers.isEmpty());
    removeButton->setVisible(!vouchers.isEmpty());

    edit->setEnabled(due > 0);
    edit->setPlaceholderText(AguaraMoney::format(due));
    if (due == 0)
        edit->clear();
    showChange();
    if (due > 0)
        edit->setFocus();
    else
        okButton->setFocus();
    adjustSize();
}

void PaymentDialog::showChange()
{
    if (due == 0) {
        changeLabel->clear();
        return;
    }
    Money amount = due;
    const QString text = edit->text().trimmed();
    if (!text.isEmpty() && !AguaraMoney::parse(text, &amount)) {
        changeLabel->setText(tr("Monto inválido"));
        return;
    }
    changeLabel->setText(amount < due ? tr("Faltan Gs. %1").arg(AguaraMoney::format(due - amount))
                                      : tr("Vuelto: Gs. %1").arg(AguaraMoney::format(amount - due)));
}

void PaymentDialog::accept()
{
    Money amount = due;
    const QString text = edit->text().trimmed();
    if ((!text.isEmpty() && !AguaraMoney::parse(text, &amount)) || amount < due) {
        edit->selectAll();
        edit->setFocus();
        return;
    }
    value = amount;
    QDialog::accept();
}

/// A voucher on the list is money already taken from the customer's card.
void PaymentDialog::reject()
{
    if (!vouchers.isEmpty()
        && !askConfirm(this, tr("Cancelar el cobro"),
                       tr("Se descartan los comprobantes cargados. Si ya se cobraron, hay que revertirlos en la "
                          "terminal o el banco."),
                       tr("Descartar")))
        return;
    QDialog::reject();
}

QList<SalePayment> PaymentDialog::payments() const
{
    QList<SalePayment> list = vouchers;
    if (value > 0) {
        SalePayment cash;
        cash.method = QString::fromLatin1(PaymentMethod::Cash);
        cash.amount = value;
        list << cash;
    }
    return list;
}

// --- VoucherDialog -----------------------------------------------------------

VoucherDialog::VoucherDialog(const PaymentMethodInfo &method, Money limit, const QStringList &taken,
                             SalesService *sales, UserService *users, ReturnService *returns, QWidget *parent)
    : QDialog(parent), method(method), limit(limit), taken(taken), sales(sales), users(users), returns(returns)
{
    const bool transfer = method.code == QLatin1String(PaymentMethod::Transfer);
    QVBoxLayout *layout = dialogHeader(this, AguaraSale::methodName(method.code),
        creditNote() ? tr("Escriba el número de la nota de crédito.")
        : transfer ? tr("Confirme que la transferencia llegó y copie su número de operación.")
        : method.needsCode ? tr("Cobre en la terminal y copie el código del comprobante impreso.")
                           : tr("Confirme que el pago llegó."));
    auto form = new QFormLayout;
    form->setHorizontalSpacing(12);
    referenceEdit = new QLineEdit(this);
    referenceEdit->setObjectName(QStringLiteral("voucherReference"));
    if (method.needsCode)
        form->addRow(creditNote() ? tr("Número:") : transfer ? tr("Número de operación:") : tr("Código del comprobante:"), referenceEdit);
    else
        referenceEdit->hide();
    amountEdit = new QLineEdit(AguaraMoney::format(limit));
    amountEdit->setObjectName(QStringLiteral("voucherAmount"));
    amountEdit->setAlignment(Qt::AlignRight);
    form->addRow(tr("Monto:"), amountEdit);
    layout->addLayout(form);
    error = new QLabel;
    error->setObjectName(QStringLiteral("voucherError"));
    error->setWordWrap(true);
    AguaraStyle::setRole(error, AguaraStyle::Error);
    error->hide();
    layout->addWidget(error);
    layout->addWidget(acceptCancel(this, tr("Aceptar")));
    setMinimumWidth(420);
    if (method.needsCode)
        referenceEdit->setFocus();
    else
        amountEdit->setFocus();
}

bool VoucherDialog::creditNote() const
{
    return method.code == QLatin1String(PaymentMethod::CreditNote);
}

void VoucherDialog::refuse(const QString &text)
{
    error->setText(text);
    error->show();
}

void VoucherDialog::accept()
{
    QString reference = AguaraSale::normalizeReference(referenceEdit->text());
    if (method.needsCode && reference.isEmpty()) {
        refuse(tr("Escriba el código: es lo que prueba el pago."));
        referenceEdit->setFocus();
        return;
    }
    Money amount = 0;
    if (!AguaraMoney::parse(amountEdit->text(), &amount) || amount <= 0 || amount > limit) {
        refuse(tr("El monto debe ser mayor que cero y no pasar de Gs. %1: no hay vuelto.")
                   .arg(AguaraMoney::format(limit)));
        amountEdit->selectAll();
        amountEdit->setFocus();
        return;
    }

    if (creditNote()) {
        const std::optional<CreditNote> note = returns->creditNote(reference);
        if (!note) {
            refuse(returns->lastError());
            referenceEdit->selectAll();
            referenceEdit->setFocus();
            return;
        }
        if (note->balance <= 0) {
            refuse(tr("La nota de crédito %1 ya no tiene saldo.").arg(note->number));
            return;
        }
        reference = note->number;
        amount = qMin(amount, note->balance);       // the rest is paid some other way
    }
    if (!reference.isEmpty() && taken.contains(reference)) {
        refuse(tr("Ese código ya está en esta venta."));
        referenceEdit->selectAll();
        referenceEdit->setFocus();
        return;
    }

    const QString usedBy = creditNote() || reference.isEmpty() ? QString() : sales->referenceUsedBy(reference);
    if (!usedBy.isEmpty()) {
        const QString what = tr("Comprobante %1, ya usado en la venta %2").arg(reference, usedBy);
        if (!users->authorize(QStringLiteral("ventas.comprobante_repetido"), what, this)) {
            refuse(tr("Ese código ya se usó en la venta %1.").arg(usedBy));
            referenceEdit->selectAll();
            referenceEdit->setFocus();
            return;
        }
        users->audit(QStringLiteral("venta.comprobante_repetido"), QStringLiteral("comprobante"), reference, QString(),
                     tr("%1, autorizó %2").arg(what, users->lastAuthorizer()));
    }

    result.method = method.code;
    result.amount = amount;
    result.reference = reference;
    QDialog::accept();
}

// --- ArticleSearchDialog -----------------------------------------------------

ArticleSearchDialog::ArticleSearchDialog(ProductService *products, const QString &text, QWidget *parent)
    : QDialog(parent)
{
    QVBoxLayout *layout = dialogHeader(this, tr("Buscar artículo"));
    filter = new QLineEdit(text);
    filter->setObjectName(QStringLiteral("articleFilter"));
    filter->setPlaceholderText(tr("Descripción o código"));
    AguaraStyle::searchField(filter);
    filter->installEventFilter(this);
    layout->addWidget(filter);

    proxy = new QSortFilterProxyModel(this);
    proxy->setSourceModel(products->createProductTableModel(this));
    proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    proxy->setFilterKeyColumn(int(ProductColumn::Description));
    proxy->setFilterFixedString(text);

    view = new QTableView;
    view->setObjectName(QStringLiteral("articleTable"));
    view->setModel(proxy);
    view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view->setSelectionBehavior(QAbstractItemView::SelectRows);
    view->setSelectionMode(QAbstractItemView::SingleSelection);
    view->setAlternatingRowColors(true);
    view->setShowGrid(false);
    view->verticalHeader()->hide();
    view->verticalHeader()->setDefaultSectionSize(32);
    for (int c = 0; c < proxy->columnCount(); ++c) {
        const bool shown = c == int(ProductColumn::Code) || c == int(ProductColumn::Description)
                           || c == int(ProductColumn::Price) || c == int(ProductColumn::Stock);
        view->setColumnHidden(c, !shown);
    }
    view->setColumnWidth(int(ProductColumn::Code), 150);
    view->horizontalHeader()->setSectionResizeMode(int(ProductColumn::Description), QHeaderView::Stretch);
    layout->addWidget(view, 1);
    layout->addWidget(acceptCancel(this, tr("Agregar")));
    resize(720, 480);

    connect(filter, &QLineEdit::textChanged, proxy, &QSortFilterProxyModel::setFilterFixedString);
    connect(filter, &QLineEdit::textChanged, this, [this]() { view->selectRow(0); });
    connect(view, &QTableView::doubleClicked, this, &ArticleSearchDialog::accept);
    view->selectRow(0);
    filter->setFocus();
}

/// The list answers the arrows while the cursor stays in the filter.
bool ArticleSearchDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == filter && event->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent *>(event)->key();
        if (key == Qt::Key_Down || key == Qt::Key_Up) {
            const int row = view->currentIndex().row() + (key == Qt::Key_Down ? 1 : -1);
            if (row >= 0 && row < proxy->rowCount())
                view->selectRow(row);
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}

void ArticleSearchDialog::accept()
{
    const QModelIndex current = view->currentIndex();
    if (!current.isValid())
        return;
    selected = proxy->index(current.row(), int(ProductColumn::Code)).data().toString();
    QDialog::accept();
}

// --- HeldSalesDialog ---------------------------------------------------------

HeldSalesDialog::HeldSalesDialog(const QList<HeldSale> &held, QWidget *parent)
    : QDialog(parent), held(held)
{
    QVBoxLayout *layout = dialogHeader(this, tr("Ventas en espera"));
    table = makeSalesTable({ tr("Hora"), tr("Usuario"), tr("Artículos"), tr("Total") });
    table->setObjectName(QStringLiteral("heldTable"));
    table->setRowCount(held.size());
    for (int row = 0; row < held.size(); ++row) {
        const HeldSale &h = held.at(row);
        table->setItem(row, 0, new QTableWidgetItem(h.when.toString(QStringLiteral("dd/MM HH:mm"))));
        table->setItem(row, 1, new QTableWidgetItem(h.user));
        table->setItem(row, 2, rightItem(QString::number(h.draft.lines.size())));
        table->setItem(row, 3, rightItem(AguaraMoney::format(AguaraSale::totals(h.draft).total)));
    }
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->selectRow(0);
    layout->addWidget(table, 1);
    layout->addWidget(acceptCancel(this, tr("Recuperar")));
    connect(table, &QTableWidget::cellDoubleClicked, this, &QDialog::accept);
    resize(520, 340);
}

qint64 HeldSalesDialog::heldId() const
{
    const int row = table->currentRow();
    return row >= 0 && row < held.size() ? held.at(row).id : 0;
}

// --- PendingSalesDialog ------------------------------------------------------

PendingSalesDialog::PendingSalesDialog(SalesService *sales, QWidget *parent)
    : QDialog(parent), sales(sales)
{
    QVBoxLayout *layout = dialogHeader(this, tr("Ventas por subir"),
        tr("Están guardadas en este equipo y se suben solas al volver la conexión. La caja no cierra hasta que lleguen."));
    table = makeSalesTable({ tr("Número"), tr("Hora"), tr("Usuario"), tr("Total"), tr("Estado") });
    table->setObjectName(QStringLiteral("pendingTable"));
    table->setColumnWidth(0, 120);
    table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    layout->addWidget(table, 1);

    auto buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->button(QDialogButtonBox::Close)->setText(tr("Cerrar"));
    auto retry = buttons->addButton(tr("Reintentar ahora"), QDialogButtonBox::ActionRole);
    retry->setObjectName(QStringLiteral("pendingRetry"));
    retry->setAutoDefault(false);
    connect(retry, &QPushButton::clicked, this, [this, sales]() {
        sales->flushPending();
        reload();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    resize(680, 380);
    reload();
}

void PendingSalesDialog::reload()
{
    const QList<PendingSale> list = sales->pendingSales();
    table->setRowCount(list.size());
    for (int row = 0; row < list.size(); ++row) {
        const PendingSale &p = list.at(row);
        table->setItem(row, 0, new QTableWidgetItem(AguaraSale::formatNumber(p.registerId, p.number)));
        table->setItem(row, 1, new QTableWidgetItem(p.when.toString(QStringLiteral("dd/MM HH:mm"))));
        table->setItem(row, 2, new QTableWidgetItem(p.user));
        table->setItem(row, 3, rightItem(AguaraMoney::format(p.total)));
        table->setItem(row, 4, new QTableWidgetItem(p.error.isEmpty() ? tr("Esperando conexión") : p.error));
    }
}

// --- SessionSalesDialog ------------------------------------------------------

SessionSalesDialog::SessionSalesDialog(SalesService *sales, UserService *users, PrintService *print, qint64 sessionId,
                                       QWidget *parent)
    : QDialog(parent), sales(sales), users(users), print(print), sessionId(sessionId)
{
    QVBoxLayout *layout = dialogHeader(this, tr("Ventas de la sesión"));
    table = makeSalesTable({ tr("Número"), tr("Hora"), tr("Usuario"), tr("Total"), tr("Estado") });
    table->setObjectName(QStringLiteral("sessionSalesTable"));
    table->setColumnWidth(0, 120);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    layout->addWidget(table, 1);

    auto row = new QHBoxLayout;
    auto voidButton = new QPushButton(AguaraStyle::icon(QStringLiteral("trash"), AguaraStyle::color(AguaraStyle::Color::Danger)),
                                      tr("Anular venta"));
    voidButton->setObjectName(QStringLiteral("voidSaleButton"));
    voidButton->setAutoDefault(false);
    AguaraStyle::setRole(voidButton, AguaraStyle::Danger);
    row->addWidget(voidButton);
    auto printButton = new QPushButton(tr("Imprimir ticket"));
    printButton->setObjectName(QStringLiteral("printSaleButton"));
    printButton->setAutoDefault(false);
    row->addWidget(printButton);
    auto pdfButton = new QPushButton(tr("Guardar PDF"));
    pdfButton->setObjectName(QStringLiteral("saleToPdfButton"));
    pdfButton->setAutoDefault(false);
    row->addWidget(pdfButton);
    row->addStretch();
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->button(QDialogButtonBox::Close)->setText(tr("Cerrar"));
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    row->addWidget(buttons);
    layout->addLayout(row);
    resize(720, 440);

    connect(voidButton, &QPushButton::clicked, this, &SessionSalesDialog::voidSelected);
    connect(printButton, &QPushButton::clicked, this, &SessionSalesDialog::printSelected);
    connect(pdfButton, &QPushButton::clicked, this, &SessionSalesDialog::saveSelected);
    reload();
}

void SessionSalesDialog::reload()
{
    list = sales->sessionSales(sessionId);
    table->setRowCount(list.size());
    for (int row = 0; row < list.size(); ++row) {
        const SaleRecord &s = list.at(row);
        table->setItem(row, 0, new QTableWidgetItem(AguaraSale::formatNumber(s.registerId, s.number)));
        table->setItem(row, 1, new QTableWidgetItem(s.when.toString(QStringLiteral("HH:mm"))));
        table->setItem(row, 2, new QTableWidgetItem(s.user));
        table->setItem(row, 3, rightItem(AguaraMoney::format(s.totals.total)));
        table->setItem(row, 4, new QTableWidgetItem(s.state == QLatin1String(SaleState::Voided) ? tr("Anulada") : tr("Cobrada")));
    }
    if (!list.isEmpty())
        table->selectRow(0);
}

void SessionSalesDialog::voidSelected()
{
    const int row = table->currentRow();
    if (row < 0 || row >= list.size())
        return;
    const SaleRecord record = list.at(row);
    const QString number = AguaraSale::formatNumber(record.registerId, record.number);
    if (!users->authorize(QStringLiteral("ventas.anular"), tr("Anular la venta %1").arg(number), this))
        return;

    // Only cash leaves the drawer: a card or a transfer is reversed elsewhere
    Money cash = 0;
    QStringList vouchers;
    const std::optional<SaleRecord> full = sales->sale(record.id);
    if (full) {
        for (const SalePayment &p : full->payments) {
            if (p.method == QLatin1String(PaymentMethod::Cash))
                cash += p.amount - p.change;
            else
                vouchers << tr("%1 %2, Gs. %3").arg(AguaraSale::methodName(p.method), p.reference, AguaraMoney::format(p.amount));
        }
    }
    QString text = cash > 0 ? tr("Vuelve el stock y salen Gs. %1 de la caja.").arg(AguaraMoney::format(cash))
                            : tr("Vuelve el stock.");
    if (!vouchers.isEmpty())
        text += QLatin1Char(' ') + tr("Revierta en la terminal o el banco: %1.").arg(vouchers.join(QStringLiteral("; ")));
    const QString reason = askReason(this, tr("Anular la venta %1").arg(number), text);
    if (reason.isEmpty())
        return;
    if (!sales->voidSale(record.id, reason)) {
        showProblem(sales->lastError());
        return;
    }
    reload();
}

void SessionSalesDialog::showProblem(const QString &text)
{
    QLabel *problem = findChild<QLabel *>(QStringLiteral("voidProblem"));
    if (!problem) {
        problem = new QLabel;
        problem->setObjectName(QStringLiteral("voidProblem"));
        problem->setWordWrap(true);
        AguaraStyle::setRole(problem, AguaraStyle::Error);
        static_cast<QVBoxLayout *>(layout())->insertWidget(1, problem);
    }
    problem->setText(text);
}

/// The first ticket of a sale is its original, also if the printer failed at
/// the till. From then on it's a copy.
bool SessionSalesDialog::mayCopy(const SaleRecord &record)
{
    return record.printCount == 0
           || users->authorize(QStringLiteral("ventas.reimprimir"),
                               tr("Reimprimir la venta %1").arg(AguaraSale::formatNumber(record.registerId, record.number)), this);
}

void SessionSalesDialog::printSelected()
{
    const int row = table->currentRow();
    if (row < 0 || row >= list.size() || !mayCopy(list.at(row)))
        return;
    if (!print->printSale(list.at(row).id))
        showProblem(print->lastError());
    reload();
}

void SessionSalesDialog::saveSelected()
{
    const int row = table->currentRow();
    if (row < 0 || row >= list.size())
        return;
    const SaleRecord record = list.at(row);
    if (!mayCopy(record))
        return;
    const QString number = AguaraSale::formatNumber(record.registerId, record.number);
    const QString file = QFileDialog::getSaveFileName(this, tr("Guardar el ticket"), tr("ticket-%1.pdf").arg(number),
                                                      tr("Documento PDF (*.pdf)"));
    if (file.isEmpty())
        return;
    if (!print->saveSalePdf(record.id, file))
        showProblem(print->lastError());
    reload();
}
