#include "stockdialogs.h"
#include "labelsheet.h"

#include <formcheck.h>

#include <QAbstractItemDelegate>
#include <QComboBox>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPrintPreviewDialog>
#include <QPrinter>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSpinBox>
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>

using AguaraStyle::Color;

// --- UnitsDialog ---------------------------------------------------------------

UnitsDialog::UnitsDialog(ProductService *products, QWidget *parent)
    : QDialog(parent), products(products)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowModality(Qt::WindowModal);     // over the article dialog
    auto layout = dialogHeader(this, tr("Unidades de venta"),
        tr("Lo que se vende por peso o medida lleva decimales. Una unidad en uso no se puede quitar."));
    resize(520, 440);

    table = makeInventoryTable({ tr("Unidad"), tr("Abreviatura"), tr("Decimales") });
    table->setObjectName(QStringLiteral("unitTable"));
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    layout->addWidget(table, 1);

    name = new QLineEdit;
    name->setObjectName(QStringLiteral("unitName"));
    name->setPlaceholderText(tr("Unidad nueva, por ejemplo Caja"));
    abbreviation = new QLineEdit;
    abbreviation->setObjectName(QStringLiteral("unitAbbreviation"));
    abbreviation->setPlaceholderText(tr("cj"));
    abbreviation->setMaxLength(8);
    abbreviation->setFixedWidth(70);
    decimals = new QSpinBox;
    decimals->setObjectName(QStringLiteral("unitDecimals"));
    decimals->setRange(0, 3);
    decimals->setToolTip(tr("0 para lo que se cuenta, 3 para lo que se pesa o se mide"));
    auto addButton = new QPushButton(AguaraStyle::icon(QStringLiteral("plus")), tr("Agregar"));
    addButton->setObjectName(QStringLiteral("unitAdd"));
    addButton->setAutoDefault(false);
    auto addRow = new QHBoxLayout;
    addRow->setSpacing(8);
    addRow->addWidget(name, 1);
    addRow->addWidget(abbreviation);
    addRow->addWidget(new QLabel(tr("Decimales")));
    addRow->addWidget(decimals);
    addRow->addWidget(addButton);
    layout->addLayout(addRow);

    problem = AguaraForm::errorLabel(QStringLiteral("unitError"));
    layout->addWidget(problem);

    auto removeButton = new QPushButton(AguaraStyle::icon(QStringLiteral("trash"), AguaraStyle::color(Color::Danger)), tr("Quitar"));
    removeButton->setObjectName(QStringLiteral("unitRemove"));
    removeButton->setAutoDefault(false);
    AguaraStyle::setRole(removeButton, AguaraStyle::Danger);
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->button(QDialogButtonBox::Close)->setText(tr("Cerrar"));
    buttons->button(QDialogButtonBox::Close)->setAutoDefault(false);
    auto bottom = new QHBoxLayout;
    bottom->addWidget(removeButton);
    bottom->addStretch();
    bottom->addWidget(buttons);
    layout->addLayout(bottom);

    keepEnterInDialog(this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(addButton, &QPushButton::clicked, this, &UnitsDialog::add);
    connect(name, &QLineEdit::returnPressed, this, &UnitsDialog::add);
    connect(abbreviation, &QLineEdit::returnPressed, this, &UnitsDialog::add);
    connect(removeButton, &QPushButton::clicked, this, &UnitsDialog::remove);
    reload();
    name->setFocus();
}

void UnitsDialog::reload()
{
    const QList<StockUnit> units = products->units();
    table->setRowCount(units.size());
    for (int row = 0; row < units.size(); ++row) {
        auto unit = new QTableWidgetItem(units.at(row).name);
        unit->setData(Qt::UserRole, units.at(row).id);
        table->setItem(row, 0, unit);
        table->setItem(row, 1, new QTableWidgetItem(units.at(row).abbreviation));
        table->setItem(row, 2, new QTableWidgetItem(QString::number(units.at(row).decimals)));
    }
}

void UnitsDialog::add()
{
    problem->hide();
    StockUnit unit;
    unit.name = name->text();
    unit.abbreviation = abbreviation->text();
    unit.decimals = decimals->value();
    if (products->addUnit(unit) == 0) {
        AguaraForm::showError(problem, products->lastError(), unit.name.trimmed().isEmpty() ? name : abbreviation);
        return;
    }
    name->clear();
    abbreviation->clear();
    decimals->setValue(0);
    reload();
    name->setFocus();
    emit listChanged();
}

void UnitsDialog::remove()
{
    problem->hide();
    const int row = table->currentRow();
    if (row < 0)
        return;
    const QString unit = table->item(row, 0)->text();
    if (!AguaraForm::confirmRemoval(this, tr("Quitar unidad"),
            tr("%1 deja de ofrecerse para los artículos. Las ventas pasadas no cambian.").arg(unit), tr("Quitar")))
        return;
    if (!products->removeUnit(table->item(row, 0)->data(Qt::UserRole).toLongLong())) {
        AguaraForm::showError(problem, products->lastError());
        return;
    }
    reload();
    emit listChanged();
}

QTableWidget *makeInventoryTable(const QStringList &headers)
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

namespace {
class EnterFilter : public QObject
{
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() != QEvent::KeyPress)
            return false;
        const int key = static_cast<QKeyEvent *>(event)->key();
        return key == Qt::Key_Return || key == Qt::Key_Enter;
    }
};
}

void keepEnterInDialog(QDialog *dialog)
{
    dialog->installEventFilter(new EnterFilter(dialog));
}

void fitCodeColumn(QTableWidget *table, int column)
{
    table->setColumnWidth(column, table->fontMetrics().horizontalAdvance(QStringLiteral("00000000000000")) + 32);
}

void commitOpenEditor(QTableWidget *table)
{
    if (QWidget *editor = table->indexWidget(table->currentIndex()))
        emit table->itemDelegate()->commitData(editor);
}

void returnFocusOnEnter(QTableWidget *table, QWidget *entry)
{
    QObject::connect(table->itemDelegate(), &QAbstractItemDelegate::closeEditor, entry,
                     [entry](QWidget *, QAbstractItemDelegate::EndEditHint hint) {
                         if (hint == QAbstractItemDelegate::SubmitModelCache)
                             entry->setFocus();
                     });
}

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

bool isAuthorized(UserService *users, const QString &permission, const QString &reason, QWidget *parent)
{
    return !users || users->authorize(permission, reason, parent);
}

namespace {

QDialogButtonBox *saveCancel(QDialog *dialog, const QString &save)
{
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(save);
    buttons->button(QDialogButtonBox::Ok)->setDefault(true);
    AguaraStyle::setRole(buttons->button(QDialogButtonBox::Ok), AguaraStyle::Primary);
    buttons->button(QDialogButtonBox::Cancel)->setText(QObject::tr("Cancelar"));
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    return buttons;
}

QTableWidgetItem *rightAligned(const QString &text)
{
    auto item = new QTableWidgetItem(text);
    item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return item;
}

QString movementName(const QString &type)
{
    if (type == QLatin1String(StockMovementType::Initial))    return QObject::tr("Saldo inicial");
    if (type == QLatin1String(StockMovementType::Purchase))   return QObject::tr("Compra");
    if (type == QLatin1String(StockMovementType::Sale))       return QObject::tr("Venta");
    if (type == QLatin1String(StockMovementType::Return))     return QObject::tr("Devolución");
    if (type == QLatin1String(StockMovementType::Adjustment)) return QObject::tr("Ajuste");
    if (type == QLatin1String(StockMovementType::Count))      return QObject::tr("Recuento");
    return type;
}

} // namespace

// --- AdjustStockDialog ---------------------------------------------------------

AdjustStockDialog::AdjustStockDialog(InventoryService *inventory, const Product &product, QWidget *parent)
    : QDialog(parent), inventory(inventory), product(product)
{
    auto layout = dialogHeader(this, tr("Ajustar stock"),
        tr("%1\nStock actual: %2 %3").arg(product.description,
            AguaraQuantity::format(product.stock, product.unit.decimals), product.unit.abbreviation));
    setMinimumWidth(420);

    direction = new QComboBox;
    direction->addItem(tr("Salida (baja el stock)"), -1);
    direction->addItem(tr("Entrada (sube el stock)"), 1);
    quantity = new QLineEdit;
    quantity->setObjectName(QStringLiteral("adjustQuantity"));
    quantity->setPlaceholderText(tr("Cantidad"));
    reason = new QComboBox;
    reason->setObjectName(QStringLiteral("adjustReason"));
    reason->setEditable(true);
    reason->addItems({ tr("Rotura"), tr("Vencimiento"), tr("Robo o faltante"), tr("Consumo interno"), tr("Corrección") });
    result = new QLabel;
    AguaraStyle::setRole(result, AguaraStyle::Muted);

    auto form = new QFormLayout;
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(10);
    form->addRow(tr("Tipo:"), direction);
    form->addRow(AguaraForm::required(tr("Cantidad:")), quantity);
    form->addRow(AguaraForm::required(tr("Motivo:")), reason);
    form->addRow(tr("Queda:"), result);
    layout->addLayout(form);
    layout->addWidget(AguaraForm::requiredNote());
    problem = AguaraForm::errorLabel(QStringLiteral("adjustError"));
    layout->addWidget(problem);
    layout->addWidget(saveCancel(this, tr("Ajustar")));

    connect(quantity, &QLineEdit::textChanged, this, &AdjustStockDialog::updateResult);
    connect(direction, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &AdjustStockDialog::updateResult);
    updateResult();
    quantity->setFocus();
}

void AdjustStockDialog::updateResult()
{
    Quantity amount = 0;
    if (!AguaraQuantity::parse(quantity->text(), &amount)) {
        result->setText(QStringLiteral("—"));
        return;
    }
    const Quantity after = product.stock + amount * direction->currentData().toInt();
    result->setText(after < 0 ? tr("%1 (el stock queda negativo)").arg(AguaraQuantity::format(after, product.unit.decimals))
                              : AguaraQuantity::format(after, product.unit.decimals));
}

void AdjustStockDialog::accept()
{
    Quantity amount = 0;
    if (!AguaraQuantity::parse(quantity->text(), &amount) || amount <= 0) {
        AguaraForm::showError(problem, tr("Escriba una cantidad mayor que cero."), quantity);
        return;
    }
    if (!AguaraForm::fitsUnit(amount, product.unit.decimals)) {
        AguaraForm::showError(problem, tr("Esta unidad no admite tantos decimales."), quantity);
        return;
    }
    if (reason->currentText().trimmed().isEmpty()) {
        AguaraForm::showError(problem, tr("Indique el motivo del ajuste."), reason);
        return;
    }
    if (!inventory->adjustStock(product.code, amount * direction->currentData().toInt(), reason->currentText())) {
        AguaraForm::showError(problem, inventory->lastError());
        return;
    }
    QDialog::accept();
}

// --- ArticleHistoryDialog ------------------------------------------------------

ArticleHistoryDialog::ArticleHistoryDialog(InventoryService *inventory, UserService *users, const Product &product, QWidget *parent)
    : QDialog(parent), inventory(inventory), users(users), product(product)
{
    auto layout = dialogHeader(this, product.description,
        tr("Código %1 · stock %2 %3").arg(product.code, AguaraQuantity::format(product.stock, product.unit.decimals),
                                          product.unit.abbreviation));
    resize(860, 560);

    auto tabs = new QTabWidget;
    tabs->setDocumentMode(true);

    const QList<StockMovement> moves = inventory->movements(product.code);
    auto kardex = makeInventoryTable({ tr("Fecha"), tr("Tipo"), tr("Cantidad"), tr("Saldo"), tr("Detalle"), tr("Usuario") });
    kardex->setObjectName(QStringLiteral("kardexTable"));
    kardex->setRowCount(moves.size());
    for (int row = 0; row < moves.size(); ++row) {
        const StockMovement &m = moves.at(row);
        kardex->setItem(row, 0, new QTableWidgetItem(m.when.toString(QStringLiteral("dd/MM/yyyy HH:mm"))));
        kardex->setItem(row, 1, new QTableWidgetItem(movementName(m.type)));
        const QString amount = AguaraQuantity::format(m.quantity, product.unit.decimals);
        kardex->setItem(row, 2, rightAligned(m.quantity > 0 ? QLatin1Char('+') + amount : amount));
        kardex->setItem(row, 3, rightAligned(AguaraQuantity::format(m.balance, product.unit.decimals)));
        kardex->setItem(row, 4, new QTableWidgetItem(m.document.isEmpty() ? m.reason : m.document));
        kardex->setItem(row, 5, new QTableWidgetItem(m.user));
    }
    kardex->resizeColumnsToContents();
    kardex->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    tabs->addTab(kardex, tr("Movimientos"));
    tabs->addTab(buildLots(), tr("Lotes"));

    const QList<PriceChange> changes = inventory->priceHistory(product.code);
    auto prices = makeInventoryTable({ tr("Fecha"), tr("Precio antes"), tr("Precio"), tr("Costo antes"), tr("Costo"),
                                       tr("Motivo"), tr("Usuario") });
    prices->setRowCount(changes.size());
    for (int row = 0; row < changes.size(); ++row) {
        const PriceChange &c = changes.at(row);
        prices->setItem(row, 0, new QTableWidgetItem(c.when.toString(QStringLiteral("dd/MM/yyyy HH:mm"))));
        prices->setItem(row, 1, rightAligned(AguaraMoney::format(c.oldPrice)));
        prices->setItem(row, 2, rightAligned(AguaraMoney::format(c.newPrice)));
        prices->setItem(row, 3, rightAligned(AguaraMoney::format(c.oldCost)));
        prices->setItem(row, 4, rightAligned(AguaraMoney::format(c.newCost)));
        prices->setItem(row, 5, new QTableWidgetItem(c.reason));
        prices->setItem(row, 6, new QTableWidgetItem(c.user));
    }
    prices->resizeColumnsToContents();
    prices->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    tabs->addTab(prices, tr("Precios"));

    auto codes = new QWidget;
    auto codesLayout = new QVBoxLayout(codes);
    codesLayout->setContentsMargins(0, 12, 0, 0);
    auto note = new QLabel(tr("Otros códigos de barras que también identifican a este artículo, por ejemplo "
                              "otra presentación u otro proveedor."));
    note->setWordWrap(true);
    AguaraStyle::setRole(note, AguaraStyle::Muted);
    codesLayout->addWidget(note);
    barcodes = new QListWidget;
    codesLayout->addWidget(barcodes, 1);
    auto addRow = new QHBoxLayout;
    auto newCode = new QLineEdit;
    newCode->setObjectName(QStringLiteral("newBarcode"));
    newCode->setPlaceholderText(tr("Escanee o escriba el código"));
    newCode->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("^[0-9A-Za-z-]{0,20}$")), newCode));
    auto add = new QPushButton(AguaraStyle::icon(QStringLiteral("plus")), tr("Agregar"));
    add->setAutoDefault(false);
    auto remove = new QPushButton(AguaraStyle::icon(QStringLiteral("trash"), AguaraStyle::color(Color::Danger)), tr("Quitar"));
    remove->setAutoDefault(false);
    AguaraStyle::setRole(remove, AguaraStyle::Danger);
    addRow->addWidget(newCode, 1);
    addRow->addWidget(add);
    addRow->addWidget(remove);
    codesLayout->addLayout(addRow);
    tabs->addTab(codes, tr("Códigos de barras"));

    const auto addCode = [this, newCode]() {
        if (newCode->text().trimmed().isEmpty()) {      // nothing for a supervisor to come for
            newCode->setFocus();
            return;
        }
        if (!isAuthorized(this->users, QStringLiteral("inventario.editar"), tr("Agregar un código de barras"), this))
            return;
        if (!this->inventory->addBarcode(this->product.code, newCode->text())) {
            QMessageBox::warning(this, tr("No se pudo agregar"), this->inventory->lastError());
            return;
        }
        newCode->clear();
        reloadBarcodes();
    };
    connect(add, &QPushButton::clicked, this, addCode);
    connect(newCode, &QLineEdit::returnPressed, this, addCode);
    keepEnterInDialog(this);
    connect(remove, &QPushButton::clicked, this, [this]() {
        const auto item = barcodes->currentItem();
        if (!item || !AguaraForm::confirmRemoval(this, tr("Quitar el código de barras"),
                tr("El código %1 deja de identificar a este artículo: al escanearlo, no se encontrará.").arg(item->text()),
                tr("Quitar")))
            return;
        if (!isAuthorized(this->users, QStringLiteral("inventario.editar"), tr("Quitar un código de barras"), this))
            return;
        if (!this->inventory->removeBarcode(item->text()))
            QMessageBox::warning(this, tr("No se pudo quitar"), this->inventory->lastError());
        reloadBarcodes();
    });
    reloadBarcodes();

    layout->addWidget(tabs, 1);
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->button(QDialogButtonBox::Close)->setText(tr("Cerrar"));
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

QWidget *ArticleHistoryDialog::buildLots()
{
    auto page = new QWidget;
    auto layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 12, 0, 0);
    auto note = new QLabel(tr("Lo que sale deja primero la fecha más próxima, y al final el stock sin fecha. "
                              "Para sacar un lote vencido, use Más > Ajustar stock."));
    note->setWordWrap(true);
    AguaraStyle::setRole(note, AguaraStyle::Muted);
    layout->addWidget(note);

    lots = makeInventoryTable({ tr("Vence"), tr("Cantidad"), tr("Estado") });
    lots->setObjectName(QStringLiteral("lotTable"));
    lots->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    layout->addWidget(lots, 1);
    undated = new QLabel;
    undated->setObjectName(QStringLiteral("lotUndated"));
    layout->addWidget(undated);

    auto quantity = new QLineEdit;
    quantity->setObjectName(QStringLiteral("lotQuantity"));
    quantity->setPlaceholderText(tr("Cantidad"));
    quantity->setFixedWidth(110);
    auto date = new QDateEdit(QDate::currentDate());
    date->setObjectName(QStringLiteral("lotDate"));
    date->setCalendarPopup(true);
    date->setDisplayFormat(QStringLiteral("dd/MM/yyyy"));
    auto add = new QPushButton(AguaraStyle::icon(QStringLiteral("plus")), tr("Poner fecha"));
    add->setObjectName(QStringLiteral("lotAdd"));
    add->setAutoDefault(false);
    add->setToolTip(tr("Da esta fecha a stock que no tiene"));
    auto remove = new QPushButton(AguaraStyle::icon(QStringLiteral("trash"), AguaraStyle::color(Color::Danger)), tr("Quitar fecha"));
    remove->setObjectName(QStringLiteral("lotRemove"));
    remove->setAutoDefault(false);
    remove->setToolTip(tr("Para una fecha equivocada: el stock queda, sin fecha"));
    AguaraStyle::setRole(remove, AguaraStyle::Danger);
    auto row = new QHBoxLayout;
    row->setSpacing(8);
    row->addWidget(quantity);
    row->addWidget(new QLabel(tr("vence")));
    row->addWidget(date);
    row->addWidget(add);
    row->addStretch();
    row->addWidget(remove);
    layout->addLayout(row);

    connect(add, &QPushButton::clicked, this, [this, quantity, date]() {
        Quantity amount = 0;
        if (!AguaraQuantity::parse(quantity->text(), &amount) || amount <= 0
            || !AguaraForm::fitsUnit(amount, product.unit.decimals)) {
            QMessageBox::warning(this, tr("Poner fecha"), tr("Escriba la cantidad, por ejemplo 24 o 2,750."));
            return;
        }
        if (!isAuthorized(users, QStringLiteral("inventario.editar"), tr("Poner fecha a un lote"), this))
            return;
        if (!inventory->addLot(product.code, amount, date->date())) {
            QMessageBox::warning(this, tr("Poner fecha"), inventory->lastError());
            return;
        }
        quantity->clear();
        reloadLots();
    });
    connect(remove, &QPushButton::clicked, this, [this]() {
        const int row = lots->currentRow();
        if (row < 0 || !AguaraForm::confirmRemoval(this, tr("Quitar fecha"),
                tr("El stock de este lote queda, sin fecha, y deja de avisar."), tr("Quitar fecha")))
            return;
        if (!isAuthorized(users, QStringLiteral("inventario.editar"), tr("Quitar la fecha de un lote"), this))
            return;
        if (!inventory->removeLot(lots->item(row, 0)->data(Qt::UserRole).toLongLong()))
            QMessageBox::warning(this, tr("Quitar fecha"), inventory->lastError());
        reloadLots();
    });
    reloadLots();
    return page;
}

void ArticleHistoryDialog::reloadLots()
{
    const QList<StockLot> list = inventory->lots(product.code);
    const QDate today = QDate::currentDate();
    Quantity dated = 0;
    lots->setRowCount(list.size());
    for (int row = 0; row < list.size(); ++row) {
        const StockLot &lot = list.at(row);
        dated += lot.quantity;
        auto date = new QTableWidgetItem(lot.expires.toString(QStringLiteral("dd/MM/yyyy")));
        date->setData(Qt::UserRole, lot.id);
        lots->setItem(row, 0, date);
        lots->setItem(row, 1, rightAligned(AguaraQuantity::format(lot.quantity, product.unit.decimals)));
        const bool past = lot.expires < today;
        lots->setItem(row, 2, new QTableWidgetItem(past ? tr("Vencido") : tr("Vigente")));
        if (past)
            lots->item(row, 2)->setForeground(AguaraStyle::color(Color::Danger));
    }
    undated->setText(tr("Sin fecha: %1 %2").arg(AguaraQuantity::format(qMax(product.stock - dated, Quantity(0)),
                                                                       product.unit.decimals), product.unit.abbreviation));
}

void ArticleHistoryDialog::reloadBarcodes()
{
    barcodes->clear();
    barcodes->addItems(inventory->barcodes(product.code));
}

// --- PriceChangeDialog ---------------------------------------------------------

PriceChangeDialog::PriceChangeDialog(ProductService *products, InventoryService *inventory, QWidget *parent)
    : QDialog(parent), inventory(inventory)
{
    auto layout = dialogHeader(this, tr("Cambio de precios"),
        tr("Sube o baja el precio de varios artículos. Revise la vista previa antes de aplicar."));
    resize(720, 600);

    category = new QComboBox;
    category->addItem(tr("Todos los rubros"), qint64(0));
    for (const NamedItem &c : products->categories())
        category->addItem(c.path, c.id);
    brand = new QComboBox;
    brand->addItem(tr("Todas las marcas"), qint64(0));
    for (const NamedItem &b : products->brands())
        brand->addItem(b.name, b.id);
    supplier = new QComboBox;
    supplier->addItem(tr("Todos los proveedores"), qint64(0));
    for (const Supplier &s : inventory->suppliers())
        supplier->addItem(s.name, s.id);

    change = new QDoubleSpinBox;
    change->setRange(-90, 500);
    change->setDecimals(2);
    change->setSuffix(QStringLiteral(" %"));
    change->setValue(10);
    rounding = new QComboBox;
    rounding->addItem(tr("Sin redondear"), qint64(0));
    for (qint64 step : { 50, 100, 500, 1000 })
        rounding->addItem(tr("Hacia arriba, de a %1 Gs.").arg(AguaraMoney::format(step)), step);
    rounding->setCurrentIndex(2);

    auto form = new QFormLayout;
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(8);
    form->addRow(tr("Rubro:"), category);
    form->addRow(tr("Marca:"), brand);
    form->addRow(tr("Proveedor:"), supplier);
    form->addRow(tr("Cambio:"), change);
    form->addRow(tr("Redondeo:"), rounding);
    layout->addLayout(form);

    preview = makeInventoryTable({ tr("Código"), tr("Descripción"), tr("Precio actual"), tr("Precio nuevo") });
    preview->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    fitCodeColumn(preview);
    layout->addWidget(preview, 1);
    summary = new QLabel;
    summary->setObjectName(QStringLiteral("priceSummary"));
    AguaraStyle::setRole(summary, AguaraStyle::Muted);
    layout->addWidget(summary);
    QDialogButtonBox *buttons = saveCancel(this, tr("Aplicar"));
    applyButton = buttons->button(QDialogButtonBox::Ok);
    applyButton->setObjectName(QStringLiteral("priceApply"));
    layout->addWidget(buttons);

    for (QComboBox *box : { category, brand, supplier, rounding })
        connect(box, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PriceChangeDialog::updatePreview);
    connect(change, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &PriceChangeDialog::updatePreview);
    updatePreview();
}

PriceFilter PriceChangeDialog::filter() const
{
    PriceFilter f;
    f.categoryId = category->currentData().toLongLong();
    f.brandId = brand->currentData().toLongLong();
    f.supplierId = supplier->currentData().toLongLong();
    return f;
}

int PriceChangeDialog::percent() const
{
    return qRound(change->value() * 100);
}

Money PriceChangeDialog::roundTo() const
{
    return rounding->currentData().toLongLong();
}

void PriceChangeDialog::updatePreview()
{
    const QList<PricePreview> list = inventory->previewPriceChange(filter(), percent(), roundTo());
    preview->setRowCount(list.size());
    for (int row = 0; row < list.size(); ++row) {
        const PricePreview &p = list.at(row);
        preview->setItem(row, 0, new QTableWidgetItem(p.code));
        preview->setItem(row, 1, new QTableWidgetItem(p.description));
        preview->setItem(row, 2, rightAligned(AguaraMoney::format(p.oldPrice)));
        preview->setItem(row, 3, rightAligned(AguaraMoney::format(p.newPrice)));
    }
    summary->setText(percent() == 0 ? tr("Escriba un cambio distinto de 0 %.")
                     : list.isEmpty() ? tr("Ningún artículo cumple esas condiciones.")
                     : list.size() == 1 ? tr("1 artículo") : tr("%1 artículos").arg(list.size()));
    applyButton->setEnabled(percent() != 0 && !list.isEmpty());
}

void PriceChangeDialog::accept()
{
    const QMessageBox::StandardButton answer = QMessageBox::question(this, tr("Cambio de precios"),
        tr("¿Cambiar el precio de %1 artículos?").arg(preview->rowCount()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;
    int changed = 0;
    if (!inventory->applyPriceChange(filter(), percent(), roundTo(), &changed)) {
        QMessageBox::warning(this, tr("No se pudo aplicar"), inventory->lastError());
        return;
    }
    QDialog::accept();
}

namespace {

void previewLabels(const QList<Product> &labels, QWidget *parent)
{
    QPrinter printer(QPrinter::HighResolution);
    printer.setPageSize(QPageSize(QPageSize::A4));
    printer.setPageMargins(QMarginsF(8, 8, 8, 8), QPageLayout::Millimeter);
    QPrintPreviewDialog preview(&printer, parent);
    preview.setWindowTitle(QObject::tr("Etiquetas de góndola"));
    QObject::connect(&preview, &QPrintPreviewDialog::paintRequested, &preview,
                     [&labels](QPrinter *p) { LabelSheet::print(p, labels); });
    preview.exec();
}

} // namespace

// --- LabelsDialog --------------------------------------------------------------

LabelsDialog::LabelsDialog(ProductService *products, InventoryService *inventory, const QString &selectedCode, QWidget *parent)
    : QDialog(parent), products(products), inventory(inventory), selectedCode(selectedCode)
{
    auto layout = dialogHeader(this, tr("Etiquetas de góndola"), tr("Hoja A4 con %1 etiquetas.")
                                   .arg(LabelSheet::Columns * LabelSheet::Rows));
    setMinimumWidth(420);

    onlySelected = new QRadioButton(tr("El artículo seleccionado"));
    onlySelected->setEnabled(!selectedCode.isEmpty());
    auto wholeCategory = new QRadioButton(tr("Todos los artículos de un rubro"));
    (selectedCode.isEmpty() ? wholeCategory : onlySelected)->setChecked(true);
    category = new QComboBox;
    category->addItem(tr("Todos los rubros"), qint64(0));
    for (const NamedItem &c : products->categories())
        category->addItem(c.path, c.id);
    copies = new QSpinBox;
    copies->setRange(1, 100);

    layout->addWidget(onlySelected);
    layout->addWidget(wholeCategory);
    auto form = new QFormLayout;
    form->setHorizontalSpacing(12);
    form->addRow(tr("Rubro:"), category);
    form->addRow(tr("Copias de cada una:"), copies);
    layout->addLayout(form);
    layout->addWidget(saveCancel(this, tr("Ver e imprimir")));
}

void LabelsDialog::accept()
{
    QStringList codes;
    if (onlySelected->isChecked()) {
        codes << selectedCode;
    } else {
        for (const CountLine &line : inventory->countSheet(category->currentData().toLongLong()))
            codes << line.code;
    }

    QList<Product> labels;
    for (const QString &code : qAsConst(codes)) {
        if (const auto p = products->product(code)) {
            for (int i = 0; i < copies->value(); ++i)
                labels << *p;
        }
    }
    if (labels.isEmpty()) {
        QMessageBox::information(this, tr("Etiquetas"), tr("No hay artículos para imprimir."));
        return;
    }

    previewLabels(labels, this);
    QDialog::accept();
}

// --- EntryLabelsDialog ---------------------------------------------------------

EntryLabelsDialog::EntryLabelsDialog(ProductService *products, const QList<PurchaseLine> &lines, QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("entryLabels"));
    auto layout = dialogHeader(this, tr("Etiquetas de lo que entró"),
        tr("Una por unidad recibida; lo que se vende por peso, una sola. Ponga 0 en lo que no necesita etiqueta."));
    resize(620, 460);
    auto table = makeInventoryTable({ tr("Artículo"), tr("Precio"), tr("Etiquetas") });
    table->setObjectName(QStringLiteral("entryLabelsTable"));
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (const PurchaseLine &line : lines) {
        const std::optional<Product> p = products->product(line.code);
        if (!p)
            continue;
        const int row = table->rowCount();
        table->insertRow(row);
        table->setItem(row, 0, new QTableWidgetItem(p->description));
        table->setItem(row, 1, rightAligned(AguaraMoney::format(p->price)));
        auto spin = new QSpinBox;
        spin->setRange(0, 500);
        spin->setValue(p->unit.decimals > 0 ? 1 : int(qMin<Quantity>(line.quantity / AguaraQuantity::One, 500)));
        table->setCellWidget(row, 2, spin);
        articles << *p;
        copies << spin;
    }
    layout->addWidget(table, 1);
    problem = AguaraForm::errorLabel(QStringLiteral("entryLabelsError"));
    layout->addWidget(problem);
    QDialogButtonBox *buttons = saveCancel(this, tr("Ver e imprimir"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Ahora no"));
    layout->addWidget(buttons);
}

void EntryLabelsDialog::accept()
{
    QList<Product> labels;
    for (int i = 0; i < articles.size(); ++i) {
        for (int n = 0; n < copies.at(i)->value(); ++n)
            labels << articles.at(i);
    }
    if (labels.isEmpty()) {
        AguaraForm::showError(problem, tr("No hay etiquetas para imprimir."));
        return;
    }
    previewLabels(labels, this);
    QDialog::accept();
}
