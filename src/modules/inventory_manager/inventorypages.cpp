#include "inventorypages.h"
#include "stockdialogs.h"
#include "additemdialog.h"

#include <formcheck.h>
#include <livesearch.h>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

using AguaraStyle::Color;

namespace {

QVBoxLayout *pageLayout(QWidget *page)
{
    auto layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 16, 24, 20);
    layout->setSpacing(12);
    return layout;
}

QTableWidgetItem *rightAligned(const QString &text)
{
    auto item = new QTableWidgetItem(text);
    item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return item;
}

QPushButton *primaryButton(const QString &text)
{
    auto button = new QPushButton(AguaraStyle::icon(QStringLiteral("plus"), AguaraStyle::color(Color::OnAccent)), text);
    AguaraStyle::setRole(button, AguaraStyle::Primary);
    return button;
}

/// A cell the user can't type in
QTableWidgetItem *readOnly(QTableWidgetItem *item)
{
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

} // namespace

// --- SuppliersPage -------------------------------------------------------------

SuppliersPage::SuppliersPage(InventoryService *inventory, UserService *users, QWidget *parent)
    : QWidget(parent), inventory(inventory), users(users)
{
    auto layout = pageLayout(this);
    auto bar = new QHBoxLayout;
    search = new QLineEdit;
    search->setObjectName(QStringLiteral("supplierSearch"));
    search->setPlaceholderText(tr("Nombre, contacto o teléfono"));
    AguaraStyle::searchField(search);
    bar->addWidget(search, 1);
    bar->addSpacing(8);
    auto remove = new QPushButton(AguaraStyle::icon(QStringLiteral("trash"), AguaraStyle::color(Color::Danger)), tr("Dar de baja"));
    AguaraStyle::setRole(remove, AguaraStyle::Danger);
    auto editButton = new QPushButton(AguaraStyle::icon(QStringLiteral("edit")), tr("Editar"));
    auto add = primaryButton(tr("Nuevo proveedor"));
    bar->addWidget(remove);
    bar->addWidget(editButton);
    bar->addWidget(add);
    layout->addLayout(bar);

    table = makeInventoryTable({ tr("Nombre"), tr("Contacto"), tr("Teléfono"), tr("Email"), tr("Dirección") });
    table->setObjectName(QStringLiteral("suppliersTable"));
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    layout->addWidget(table, 1);

    const auto editSelected = [this]() {
        const int row = table->currentRow();
        if (row >= 0 && row < list.size())
            edit(list.at(row));
    };
    connect(search, &QLineEdit::textChanged, this, [this](const QString &text) { AguaraSearch::filterTable(table, text); });
    connect(add, &QPushButton::clicked, this, [this]() { edit(Supplier()); });
    connect(editButton, &QPushButton::clicked, this, editSelected);
    connect(table, &QTableWidget::activated, this, editSelected);
    connect(remove, &QPushButton::clicked, this, [this]() {
        const int row = table->currentRow();
        if (row < 0 || row >= list.size())
            return;
        const Supplier s = list.at(row);
        if (!AguaraForm::confirmRemoval(this, tr("Dar de baja"),
                tr("%1 deja de aparecer en la lista y al cargar compras. Sus compras no cambian.").arg(s.name),
                tr("Dar de baja")))
            return;
        if (!isAuthorized(this->users, QStringLiteral("inventario.proveedores"), tr("Dar de baja a %1").arg(s.name), this))
            return;
        if (!this->inventory->deactivateSupplier(s.id))
            QMessageBox::warning(this, tr("No se pudo dar de baja"), this->inventory->lastError());
        reload();
    });
    reload();
}

void SuppliersPage::reload()
{
    list = inventory->suppliers();
    table->setRowCount(list.size());
    for (int row = 0; row < list.size(); ++row) {
        const Supplier &s = list.at(row);
        table->setItem(row, 0, new QTableWidgetItem(s.name));
        table->setItem(row, 1, new QTableWidgetItem(s.contact));
        table->setItem(row, 2, new QTableWidgetItem(s.phone));
        table->setItem(row, 3, new QTableWidgetItem(s.email));
        table->setItem(row, 4, new QTableWidgetItem(s.address));
    }
    AguaraSearch::filterTable(table, search->text());
}

void SuppliersPage::edit(const Supplier &supplier)
{
    const QString reason = supplier.id ? tr("Modificar a %1").arg(supplier.name) : tr("Nuevo proveedor");
    if (!isAuthorized(users, QStringLiteral("inventario.proveedores"), reason, this))
        return;
    if (showForm(inventory, supplier, this))
        reload();
}

qint64 SuppliersPage::showForm(InventoryService *inventory, const Supplier &supplier, QWidget *parent)
{
    QDialog dialog(parent);
    auto layout = dialogHeader(&dialog, supplier.id ? tr("Modificar proveedor") : tr("Nuevo proveedor"));
    dialog.setMinimumWidth(460);
    auto form = new QFormLayout;
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(8);
    auto name = new QLineEdit(supplier.name);
    name->setObjectName(QStringLiteral("supplierName"));
    auto contact = new QLineEdit(supplier.contact);
    auto phone = new QLineEdit(supplier.phone);
    auto email = new QLineEdit(supplier.email);
    auto address = new QLineEdit(supplier.address);
    auto web = new QLineEdit(supplier.web);
    auto notes = new QLineEdit(supplier.notes);
    form->addRow(AguaraForm::required(tr("Nombre:")), name);
    form->addRow(tr("Contacto:"), contact);
    form->addRow(tr("Teléfono:"), phone);
    form->addRow(tr("Email:"), email);
    form->addRow(tr("Dirección:"), address);
    form->addRow(tr("Web:"), web);
    form->addRow(tr("Notas:"), notes);
    layout->addLayout(form);
    layout->addWidget(AguaraForm::requiredNote());
    auto problem = AguaraForm::errorLabel(QStringLiteral("supplierError"));
    layout->addWidget(problem);
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setText(tr("Guardar"));
    buttons->button(QDialogButtonBox::Save)->setDefault(true);
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancelar"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    qint64 saved = 0;
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        Supplier s = supplier;
        s.name = name->text();
        s.contact = contact->text();
        s.phone = phone->text();
        s.email = email->text();
        s.address = address->text();
        s.web = web->text();
        s.notes = notes->text();
        saved = inventory->saveSupplier(s);
        if (!saved) {
            AguaraForm::showError(problem, inventory->lastError(), s.name.trimmed().isEmpty() ? name : nullptr);
            return;
        }
        dialog.accept();
    });
    name->setFocus();
    return dialog.exec() == QDialog::Accepted ? saved : 0;
}

// --- PurchaseDialog ------------------------------------------------------------

enum LineColumn { LineCode, LineDescription, LineQuantity, LineCost, LineMargin, LinePrice, LineExpires, LineSubtotal, LineColumns };

PurchaseDialog::PurchaseDialog(ProductService *products, InventoryService *inventory, UserService *users, QWidget *parent)
    : QDialog(parent), products(products), inventory(inventory), users(users), newArticle(new addItemDialog(products, this))
{
    auto layout = dialogHeader(this, tr("Alta de productos"),
        tr("Escanee o busque cada artículo, como en una venta. En la tabla se edita la cantidad, el costo y el margen o el precio."));
    resize(980, 640);

    supplier = new QComboBox;
    supplier->setObjectName(QStringLiteral("purchaseSupplier"));
    supplier->setPlaceholderText(tr("Sin proveedores: cree uno"));
    supplier->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);    // a long name mustn't widen the window
    supplier->setMinimumContentsLength(16);
    fillSuppliers();
    auto newSupplier = new QPushButton(AguaraStyle::icon(QStringLiteral("plus")), tr("Nuevo proveedor"));
    newSupplier->setObjectName(QStringLiteral("purchaseNewSupplier"));
    newSupplier->setAutoDefault(false);
    invoice = new QLineEdit;
    invoice->setObjectName(QStringLiteral("purchaseInvoice"));
    invoice->setPlaceholderText(tr("001-001-0000123"));
    invoice->setMinimumWidth(170);                  // a whole invoice number
    date = new QDateEdit(QDate::currentDate());
    date->setObjectName(QStringLiteral("purchaseDate"));
    date->setCalendarPopup(true);
    date->setDisplayFormat(QStringLiteral("dd/MM/yyyy"));
    date->setMaximumDate(QDate::currentDate());

    auto head = new QHBoxLayout;
    head->setSpacing(8);
    head->addWidget(new QLabel(AguaraForm::required(tr("Proveedor"))));
    head->addWidget(supplier, 1);
    head->addWidget(new QLabel(tr("Factura")));
    head->addWidget(invoice);
    head->addWidget(new QLabel(tr("Fecha")));
    head->addWidget(date);
    layout->addLayout(head);

    entry = new QLineEdit;
    entry->setObjectName(QStringLiteral("purchaseEntry"));
    entry->setPlaceholderText(tr("Código o descripción del artículo"));
    AguaraStyle::searchField(entry);
    auto newButton = new QPushButton(AguaraStyle::icon(QStringLiteral("plus")), tr("Nuevo artículo"));
    newButton->setObjectName(QStringLiteral("purchaseNewArticle"));
    newButton->setAutoDefault(false);
    newButton->setToolTip(tr("Crea un artículo que todavía no existe (Ins)"));
    auto entryRow = new QHBoxLayout;
    entryRow->setSpacing(8);
    entryRow->addWidget(entry, 1);
    entryRow->addWidget(newButton);
    entryRow->addWidget(newSupplier);
    layout->addLayout(entryRow);

    lines = makeInventoryTable({ tr("Código"), tr("Descripción"), tr("Cantidad"), tr("Costo"), tr("Margen %"), tr("Precio de venta"), tr("Vencimiento"), tr("Subtotal") });
    lines->setObjectName(QStringLiteral("purchaseLines"));
    lines->setEditTriggers(QAbstractItemView::AllEditTriggers);
    lines->setSelectionBehavior(QAbstractItemView::SelectItems);
    lines->horizontalHeader()->setSectionResizeMode(LineDescription, QHeaderView::Stretch);
    fitCodeColumn(lines, LineCode);
    lines->setColumnWidth(LineQuantity, 84);        // figures: the free width is for the description
    lines->setColumnWidth(LineCost, 92);
    lines->setColumnWidth(LineMargin, 100);
    lines->setColumnWidth(LinePrice, 136);
    lines->setColumnWidth(LineExpires, 104);
    lines->setColumnWidth(LineSubtotal, 100);
    returnFocusOnEnter(lines, entry);
    layout->addWidget(lines, 1);

    auto options = new QHBoxLayout;
    options->setSpacing(8);
    updateCosts = new QCheckBox(tr("Actualizar el costo de los artículos"));
    updateCosts->setChecked(true);
    margin = new QDoubleSpinBox;
    margin->setRange(0, 90);
    margin->setSuffix(QStringLiteral(" %"));
    margin->setToolTip(tr("Margen sobre el precio. Con un valor, cada línea sugiere el precio nuevo."));
    auto removeLine = new QPushButton(AguaraStyle::icon(QStringLiteral("trash"), AguaraStyle::color(Color::Danger)), tr("Quitar línea"));
    removeLine->setAutoDefault(false);
    AguaraStyle::setRole(removeLine, AguaraStyle::Danger);
    total = new QLabel;
    total->setObjectName(QStringLiteral("purchaseTotal"));
    total->setStyleSheet(QStringLiteral("font-size: 12pt; font-weight: 600;"));
    options->addWidget(updateCosts);
    options->addSpacing(12);
    options->addWidget(new QLabel(tr("Margen sugerido")));
    options->addWidget(margin);
    options->addSpacing(12);
    options->addWidget(removeLine);
    options->addStretch();
    options->addWidget(total);
    layout->addLayout(options);
    problem = AguaraForm::errorLabel(QStringLiteral("purchaseError"));
    layout->addWidget(problem);

    auto buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setText(tr("Dar de alta"));
    buttons->button(QDialogButtonBox::Save)->setAutoDefault(false);
    AguaraStyle::setRole(buttons->button(QDialogButtonBox::Save), AguaraStyle::Primary);
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancelar"));
    buttons->button(QDialogButtonBox::Cancel)->setAutoDefault(false);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    keepEnterInDialog(this);
    connect(entry, &QLineEdit::returnPressed, this, &PurchaseDialog::addFromEntry);
    connect(new SuggestionBox(entry, SuggestionBox::articles(products, this)), &SuggestionBox::chosen,
            this, [this](const QString &code) {
                entry->setText(code);
                addFromEntry();
            });
    connect(lines, &QTableWidget::cellChanged, this, &PurchaseDialog::onCellChanged);
    connect(newButton, &QPushButton::clicked, this, [this]() { createArticle(); });
    connect(newSupplier, &QPushButton::clicked, this, &PurchaseDialog::createSupplier);
    connect(new QShortcut(QKeySequence(Qt::Key_Insert), this), &QShortcut::activated, this, [this]() { createArticle(); });
    connect(newArticle, &addItemDialog::itemAddedEdited, this, [this](const QString &code) {
        if (const std::optional<Product> created = this->products->product(code))
            addLine(*created, AguaraQuantity::One);
    });
    connect(margin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this]() {
        for (int row = 0; row < lines->rowCount(); ++row)
            updateSuggested(row);
    });
    connect(removeLine, &QPushButton::clicked, this, [this]() {
        const int row = lines->currentRow();
        if (row < 0 || !AguaraForm::confirmRemoval(this, tr("Quitar línea"),
                tr("%1 sale de esta compra.").arg(lines->item(row, LineDescription)->text()), tr("Quitar línea")))
            return;
        lines->removeRow(row);
        updateTotal();
    });
    updateTotal();
    entry->setFocus();
}

void PurchaseDialog::addFromEntry()
{
    const QString text = entry->text().trimmed();
    if (text.isEmpty())
        return;
    const ScanResult scan = inventory->resolveScan(text);
    entry->clear();
    problem->hide();
    if (scan.found) {
        addLine(scan.product, scan.quantity);
        return;
    }
    QApplication::beep();
    // digits are a code read from what just arrived: it's a new article
    const bool isCode = std::all_of(text.cbegin(), text.cend(), [](QChar c) { return c.isDigit(); });
    if (isCode && AguaraForm::validCode(text))
        createArticle(text);
    else
        AguaraForm::showError(problem, tr("No hay artículos con \"%1\". Si es nuevo, use Nuevo artículo.").arg(text), entry);
}

void PurchaseDialog::fillSuppliers()
{
    supplier->clear();
    for (const Supplier &s : inventory->suppliers())
        supplier->addItem(s.name, s.id);
    supplier->setCurrentIndex(supplier->count() > 0 ? 0 : -1);   // with a placeholder, a combo stays on none
}

/// The new one is who this alta comes from.
void PurchaseDialog::createSupplier()
{
    if (!isAuthorized(users, QStringLiteral("inventario.proveedores"), tr("Nuevo proveedor"), this))
        return;
    const qint64 id = SuppliersPage::showForm(inventory, Supplier(), this);
    if (!id)
        return;
    fillSuppliers();
    supplier->setCurrentIndex(supplier->findData(id));
    problem->hide();
    entry->setFocus();
}

void PurchaseDialog::createArticle(const QString &code)
{
    if (isAuthorized(users, QStringLiteral("inventario.editar"), tr("Nuevo artículo"), this))
        newArticle->addForEntry(code);
}

/// As in a sale: the same article again is one more on its line.
void PurchaseDialog::addLine(const Product &p, Quantity quantity)
{
    for (int row = 0; row < lines->rowCount(); ++row) {
        if (lines->item(row, LineCode)->text() != p.code)
            continue;
        Quantity current = 0;
        AguaraQuantity::parse(lines->item(row, LineQuantity)->text(), &current);
        lines->item(row, LineQuantity)->setText(AguaraQuantity::format(current + quantity, p.unit.decimals));
        lines->setCurrentCell(row, LineQuantity);
        return;
    }

    filling = true;
    const int row = lines->rowCount();
    lines->insertRow(row);
    lines->setItem(row, LineCode, readOnly(new QTableWidgetItem(p.code)));
    lines->setItem(row, LineDescription, readOnly(new QTableWidgetItem(p.description)));
    lines->setItem(row, LineQuantity, rightAligned(AguaraQuantity::format(quantity, p.unit.decimals)));
    lines->item(row, LineQuantity)->setData(Qt::UserRole, p.unit.decimals);
    lines->setItem(row, LineCost, rightAligned(AguaraMoney::format(p.cost)));
    lines->setItem(row, LineMargin, rightAligned(QString()));
    lines->setItem(row, LinePrice, rightAligned(AguaraMoney::format(p.price)));
    lines->setItem(row, LineExpires, new QTableWidgetItem(QString()));
    lines->item(row, LineExpires)->setToolTip(tr("dd/mm/aaaa, para esta cantidad. Vacío: queda sin fecha."));
    lines->setItem(row, LineSubtotal, readOnly(rightAligned(QString())));
    filling = false;
    updateSuggested(row);
    showMargin(row);
    updateTotal();
    lines->setCurrentCell(row, LineQuantity);
    lines->editItem(lines->item(row, LineQuantity));
}

namespace {

/// price = cost / (1 - margin), up to the next 100 Gs.
Money priceWithMargin(Money cost, double percent)
{
    Money price = qRound64(double(cost) / (1.0 - percent / 100.0));
    if (price % 100 != 0)
        price += 100 - price % 100;
    return price;
}

} // namespace

/// With a suggested margin for the whole list, each cost gives its price.
void PurchaseDialog::updateSuggested(int row)
{
    if (margin->value() <= 0)
        return;
    Money cost = 0;
    if (!AguaraMoney::parse(lines->item(row, LineCost)->text(), &cost) || cost <= 0)
        return;
    filling = true;
    lines->item(row, LinePrice)->setText(AguaraMoney::format(priceWithMargin(cost, margin->value())));
    filling = false;
    showMargin(row);
}

/// What the price leaves over the cost, as in the article's form.
void PurchaseDialog::showMargin(int row)
{
    Money cost = 0, price = 0;
    const bool known = AguaraMoney::parse(lines->item(row, LineCost)->text(), &cost)
                    && AguaraMoney::parse(lines->item(row, LinePrice)->text(), &price) && price > 0 && cost > 0;
    filling = true;
    lines->item(row, LineMargin)->setText(known ? QLocale().toString(100.0 * double(price - cost) / double(price), 'f', 1) : QString());
    filling = false;
}

/// A margin typed by hand sets the price. One that can't be used goes back.
void PurchaseDialog::applyMargin(int row)
{
    QString typed = lines->item(row, LineMargin)->text();
    typed.remove(QLatin1Char('%'));
    typed.replace(QLatin1Char(','), QLatin1Char('.'));
    bool number = false;
    const double percent = typed.trimmed().toDouble(&number);
    Money cost = 0;
    if (number && percent >= 0 && percent < 100
        && AguaraMoney::parse(lines->item(row, LineCost)->text(), &cost) && cost > 0) {
        filling = true;
        lines->item(row, LinePrice)->setText(AguaraMoney::format(priceWithMargin(cost, percent)));
        filling = false;
        return;
    }
    showMargin(row);
}

void PurchaseDialog::onCellChanged(int row, int column)
{
    if (filling)
        return;
    if (column == LineCost) {
        updateSuggested(row);
        showMargin(row);
    } else if (column == LineMargin) {
        applyMargin(row);
    } else if (column == LinePrice) {
        showMargin(row);
    }
    updateTotal();
}

void PurchaseDialog::updateTotal()
{
    Money sum = 0;
    filling = true;
    for (int row = 0; row < lines->rowCount(); ++row) {
        Quantity quantity = 0;
        Money cost = 0;
        const bool ok = AguaraQuantity::parse(lines->item(row, LineQuantity)->text(), &quantity)
                     && AguaraMoney::parse(lines->item(row, LineCost)->text(), &cost);
        const Money subtotal = ok ? AguaraQuantity::lineTotal(cost, quantity) : 0;
        lines->item(row, LineSubtotal)->setText(ok ? AguaraMoney::format(subtotal) : tr("revisar"));
        sum += subtotal;
    }
    filling = false;
    total->setText(tr("Total: %1").arg(AguaraMoney::formatWithSymbol(sum)));
}

Purchase PurchaseDialog::build(QString *refused) const
{
    Purchase purchase;
    purchase.supplierId = supplier->currentData().toLongLong();
    purchase.invoice = invoice->text();
    purchase.date = date->date();
    purchase.updateCosts = updateCosts->isChecked();
    for (int row = 0; row < lines->rowCount(); ++row) {
        PurchaseLine line;
        line.code = lines->item(row, LineCode)->text();
        const QString name = lines->item(row, LineDescription)->text();
        const QString price = lines->item(row, LinePrice)->text().trimmed();
        const auto refuse = [&](int column, const QString &text) {
            *refused = text;
            lines->setCurrentCell(row, column);
            return purchase;
        };
        if (!AguaraQuantity::parse(lines->item(row, LineQuantity)->text(), &line.quantity) || line.quantity <= 0)
            return refuse(LineQuantity, tr("La cantidad de %1 debe ser mayor que cero.").arg(name));
        if (!AguaraForm::fitsUnit(line.quantity, lines->item(row, LineQuantity)->data(Qt::UserRole).toInt()))
            return refuse(LineQuantity, tr("La cantidad de %1 tiene más decimales de los que admite su unidad.").arg(name));
        if (!AguaraMoney::parse(lines->item(row, LineCost)->text(), &line.unitCost) || line.unitCost < 0)
            return refuse(LineCost, tr("Revise el costo de %1.").arg(name));
        if (!price.isEmpty() && (!AguaraMoney::parse(price, &line.newPrice) || line.newPrice <= 0))
            return refuse(LinePrice, tr("Revise el precio nuevo de %1.").arg(name));
        const QString expires = lines->item(row, LineExpires)->text().trimmed();
        if (!expires.isEmpty()) {
            line.expires = QDate::fromString(expires, QStringLiteral("d/M/yyyy"));
            if (!line.expires.isValid())
                return refuse(LineExpires, tr("Escriba el vencimiento de %1 como dd/mm/aaaa.").arg(name));
            if (line.expires < QDate::currentDate())
                return refuse(LineExpires, tr("El vencimiento de %1 ya pasó.").arg(name));
        }
        purchase.lines << line;
    }
    return purchase;
}

void PurchaseDialog::accept()
{
    commitOpenEditor(lines);
    QString refused;
    const Purchase purchase = build(&refused);
    if (!refused.isEmpty()) {
        AguaraForm::showError(problem, refused);
        return;
    }
    if (!inventory->savePurchase(purchase)) {
        AguaraForm::showError(problem, inventory->lastError());
        return;
    }
    saved = purchase.lines;
    QDialog::accept();
}

// --- PurchasesPage -------------------------------------------------------------

PurchasesPage::PurchasesPage(ProductService *products, InventoryService *inventory, UserService *users, QWidget *parent)
    : QWidget(parent), products(products), inventory(inventory), users(users)
{
    auto layout = pageLayout(this);
    auto bar = new QHBoxLayout;
    bar->setSpacing(8);
    from = new QDateEdit(QDate::currentDate().addDays(-30));
    to = new QDateEdit(QDate::currentDate());
    for (QDateEdit *edit : { from, to }) {
        edit->setCalendarPopup(true);
        edit->setDisplayFormat(QStringLiteral("dd/MM/yyyy"));
    }
    AguaraForm::keepInOrder(from, to);
    bar->addWidget(new QLabel(tr("Desde")));
    bar->addWidget(from);
    bar->addWidget(new QLabel(tr("Hasta")));
    bar->addWidget(to);
    bar->addStretch();
    auto add = primaryButton(tr("Alta de productos"));
    add->setObjectName(QStringLiteral("newPurchaseButton"));
    bar->addWidget(add);
    layout->addLayout(bar);

    table = makeInventoryTable({ tr("Fecha"), tr("Proveedor"), tr("Factura"), tr("Total"), tr("Usuario") });
    table->setObjectName(QStringLiteral("purchasesTable"));
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    fitCodeColumn(table, 2);    // the invoice number
    layout->addWidget(table, 1);

    connect(from, &QDateEdit::dateChanged, this, &PurchasesPage::reload);
    connect(to, &QDateEdit::dateChanged, this, &PurchasesPage::reload);
    connect(table, &QTableWidget::activated, this, &PurchasesPage::showSelected);
    connect(add, &QPushButton::clicked, this, &PurchasesPage::receive);
    reload();
}

void PurchasesPage::receive()
{
    if (!isAuthorized(users, QStringLiteral("inventario.compras"), tr("Dar de alta productos"), this))
        return;
    PurchaseDialog dialog(products, inventory, users, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    reload();
    EntryLabelsDialog(products, dialog.received(), this).exec();
}

void PurchasesPage::reload()
{
    list = inventory->purchases(from->date(), to->date());
    table->setRowCount(list.size());
    for (int row = 0; row < list.size(); ++row) {
        const Purchase &p = list.at(row);
        table->setItem(row, 0, new QTableWidgetItem(p.date.toString(QStringLiteral("dd/MM/yyyy"))));
        table->setItem(row, 1, new QTableWidgetItem(p.supplierName));
        table->setItem(row, 2, new QTableWidgetItem(p.invoice));
        table->setItem(row, 3, rightAligned(AguaraMoney::format(p.total)));
        table->setItem(row, 4, new QTableWidgetItem(p.user));
    }
}

void PurchasesPage::showSelected()
{
    const int row = table->currentRow();
    if (row < 0 || row >= list.size())
        return;
    const std::optional<Purchase> purchase = inventory->purchase(list.at(row).id);
    if (!purchase)
        return;

    QDialog dialog(this);
    auto layout = dialogHeader(&dialog, tr("Compra a %1").arg(purchase->supplierName),
        tr("Factura %1 · %2 · %3").arg(purchase->invoice.isEmpty() ? tr("sin número") : purchase->invoice,
                                       purchase->date.toString(QStringLiteral("dd/MM/yyyy")),
                                       AguaraMoney::formatWithSymbol(purchase->total)));
    dialog.resize(760, 460);
    auto items = makeInventoryTable({ tr("Código"), tr("Descripción"), tr("Cantidad"), tr("Costo"), tr("Subtotal") });
    items->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    fitCodeColumn(items);
    items->setRowCount(purchase->lines.size());
    for (int i = 0; i < purchase->lines.size(); ++i) {
        const PurchaseLine &line = purchase->lines.at(i);
        items->setItem(i, 0, new QTableWidgetItem(line.code));
        items->setItem(i, 1, new QTableWidgetItem(line.description));
        items->setItem(i, 2, rightAligned(AguaraQuantity::format(line.quantity)));
        items->setItem(i, 3, rightAligned(AguaraMoney::format(line.unitCost)));
        items->setItem(i, 4, rightAligned(AguaraMoney::format(AguaraQuantity::lineTotal(line.unitCost, line.quantity))));
    }
    layout->addWidget(items, 1);
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->button(QDialogButtonBox::Close)->setText(tr("Cerrar"));
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.exec();
}

// --- CountsPage ----------------------------------------------------------------

enum CountColumn { CountCode, CountDescription, CountExpected, CountCounted, CountDifference };

CountsPage::CountsPage(ProductService *products, InventoryService *inventory, UserService *users, QWidget *parent)
    : QWidget(parent), products(products), inventory(inventory), users(users)
{
    auto layout = pageLayout(this);
    auto bar = new QHBoxLayout;
    bar->setSpacing(8);
    category = new QComboBox;
    category->setObjectName(QStringLiteral("countCategory"));
    category->setMinimumWidth(240);
    category->addItem(tr("Todos los rubros"), qint64(0));
    for (const NamedItem &c : products->categories())
        category->addItem(c.path, c.id);
    auto prepareButton = new QPushButton(AguaraStyle::icon(QStringLiteral("list")), tr("Preparar planilla"));
    prepareButton->setObjectName(QStringLiteral("countPrepare"));
    entry = new QLineEdit;
    entry->setObjectName(QStringLiteral("countEntry"));
    entry->setPlaceholderText(tr("Escanee o busque un artículo: suma uno a lo contado"));
    AguaraStyle::searchField(entry);
    bar->addWidget(new QLabel(tr("Rubro")));
    bar->addWidget(category);
    bar->addWidget(prepareButton);
    bar->addSpacing(8);
    bar->addWidget(entry, 1);
    layout->addLayout(bar);

    table = makeInventoryTable({ tr("Código"), tr("Descripción"), tr("Sistema"), tr("Contado"), tr("Diferencia") });
    table->setObjectName(QStringLiteral("countTable"));
    table->setEditTriggers(QAbstractItemView::AllEditTriggers);
    table->horizontalHeader()->setSectionResizeMode(CountDescription, QHeaderView::Stretch);
    fitCodeColumn(table);
    returnFocusOnEnter(table, entry);
    layout->addWidget(table, 1);

    auto bottom = new QHBoxLayout;
    summary = new QLabel;
    summary->setObjectName(QStringLiteral("countSummary"));
    AguaraStyle::setRole(summary, AguaraStyle::Muted);
    applyButton = new QPushButton(tr("Aplicar recuento"));
    applyButton->setObjectName(QStringLiteral("countApply"));
    AguaraStyle::setRole(applyButton, AguaraStyle::Primary);
    applyButton->setEnabled(false);
    bottom->addWidget(summary, 1);
    bottom->addWidget(applyButton);
    layout->addLayout(bottom);

    connect(prepareButton, &QPushButton::clicked, this, &CountsPage::prepare);
    connect(entry, &QLineEdit::returnPressed, this, &CountsPage::scan);
    connect(new SuggestionBox(entry, SuggestionBox::articles(products, this)), &SuggestionBox::chosen,
            this, [this](const QString &code) {
                entry->setText(code);
                scan();
            });
    connect(table, &QTableWidget::cellChanged, this, &CountsPage::onCellChanged);
    connect(applyButton, &QPushButton::clicked, this, &CountsPage::apply);
}

void CountsPage::prepare()
{
    sheet = inventory->countSheet(category->currentData().toLongLong());
    filling = true;
    table->setRowCount(sheet.size());
    for (int row = 0; row < sheet.size(); ++row) {
        const CountLine &line = sheet.at(row);
        table->setItem(row, CountCode, readOnly(new QTableWidgetItem(line.code)));
        table->setItem(row, CountDescription, readOnly(new QTableWidgetItem(line.description)));
        table->setItem(row, CountExpected, readOnly(rightAligned(AguaraQuantity::format(line.expected, line.decimals))));
        table->setItem(row, CountCounted, rightAligned(QString()));
        table->setItem(row, CountDifference, readOnly(rightAligned(QString())));
    }
    filling = false;
    applyButton->setEnabled(!sheet.isEmpty());
    summary->setText(sheet.size() == 1 ? tr("1 artículo. Solo se ajusta lo que se cuente.")
                                       : tr("%1 artículos. Solo se ajusta lo que se cuente.").arg(sheet.size()));
    entry->setFocus();
}

void CountsPage::scan()
{
    const QString text = entry->text().trimmed();
    entry->clear();
    if (text.isEmpty())
        return;
    const ScanResult result = inventory->resolveScan(text);
    for (int row = 0; result.found && row < sheet.size(); ++row) {
        if (sheet.at(row).code != result.product.code)
            continue;
        const Quantity counted = (sheet.at(row).wasCounted ? sheet.at(row).counted : 0) + result.quantity;
        table->item(row, CountCounted)->setText(AguaraQuantity::format(counted, sheet.at(row).decimals));
        table->setCurrentCell(row, CountCounted);
        table->scrollToItem(table->item(row, CountCounted));
        return;
    }
    QApplication::beep();
    summary->setText(tr("El código %1 no está en esta planilla.").arg(text));
}

void CountsPage::onCellChanged(int row, int column)
{
    if (!filling && column == CountCounted)
        updateRow(row);
}

void CountsPage::updateRow(int row)
{
    CountLine &line = sheet[row];
    const QString text = table->item(row, CountCounted)->text().trimmed();
    Quantity counted = 0;
    line.wasCounted = !text.isEmpty() && AguaraQuantity::parse(text, &counted)
                      && counted >= 0 && AguaraForm::fitsUnit(counted, line.decimals);
    line.counted = line.wasCounted ? counted : line.expected;

    filling = true;
    QTableWidgetItem *difference = table->item(row, CountDifference);
    if (text.isEmpty()) {
        difference->setText(QString());
    } else if (!line.wasCounted) {
        difference->setText(tr("revisar"));
    } else {
        const Quantity delta = line.counted - line.expected;
        const QString amount = AguaraQuantity::format(delta, line.decimals);
        difference->setText(delta > 0 ? QLatin1Char('+') + amount : amount);
        difference->setForeground(AguaraStyle::color(delta == 0 ? Color::Success : Color::Danger));
    }
    filling = false;
}

void CountsPage::apply()
{
    commitOpenEditor(table);
    // a cell that says "revisar" was typed: skipping it would leave the article uncounted
    for (int row = 0; row < sheet.size(); ++row) {
        if (!sheet.at(row).wasCounted && !table->item(row, CountCounted)->text().trimmed().isEmpty()) {
            summary->setText(tr("Revise lo contado de %1: no es una cantidad válida.").arg(sheet.at(row).description));
            table->setCurrentCell(row, CountCounted);
            table->scrollToItem(table->item(row, CountCounted));
            return;
        }
    }
    int counted = 0, different = 0;
    for (const CountLine &line : qAsConst(sheet)) {
        counted += line.wasCounted ? 1 : 0;
        different += line.wasCounted && line.counted != line.expected ? 1 : 0;
    }
    if (counted == 0) {
        QMessageBox::information(this, tr("Recuento"), tr("Todavía no se contó ningún artículo."));
        return;
    }
    if (!isAuthorized(users, QStringLiteral("inventario.recuento"), tr("Aplicar un recuento"), this))
        return;
    if (QMessageBox::question(this, tr("Aplicar recuento"),
            tr("Se contaron %1 artículos y %2 tienen diferencia. ¿Ajustar el stock?").arg(counted).arg(different),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;

    int adjusted = 0;
    if (!inventory->applyCount(sheet, tr("Recuento físico: %1").arg(category->currentText()), &adjusted)) {
        QMessageBox::warning(this, tr("No se pudo aplicar"), inventory->lastError());
        return;
    }
    sheet.clear();
    table->setRowCount(0);
    applyButton->setEnabled(false);
    summary->setText(adjusted == 1 ? tr("Recuento aplicado: 1 ajuste.") : tr("Recuento aplicado: %1 ajustes.").arg(adjusted));
}
