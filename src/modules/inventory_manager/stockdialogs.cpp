#include "stockdialogs.h"
#include "labelsheet.h"

#include <QAbstractItemDelegate>
#include <QComboBox>
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
#include <QSpinBox>
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>

using AguaraStyle::Color;

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
    form->addRow(tr("Cantidad:"), quantity);
    form->addRow(tr("Motivo:"), reason);
    form->addRow(tr("Queda:"), result);
    layout->addLayout(form);
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
        QMessageBox::warning(this, tr("Cantidad inválida"), tr("Escriba una cantidad mayor que cero."));
        return;
    }
    if (!inventory->adjustStock(product.code, amount * direction->currentData().toInt(), reason->currentText())) {
        QMessageBox::warning(this, tr("No se pudo ajustar"), inventory->lastError());
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
    newCode->setPlaceholderText(tr("Escanee o escriba el código"));
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
        if (!item || !isAuthorized(this->users, QStringLiteral("inventario.editar"), tr("Quitar un código de barras"), this))
            return;
        this->inventory->removeBarcode(item->text());
        reloadBarcodes();
    });
    reloadBarcodes();

    layout->addWidget(tabs, 1);
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->button(QDialogButtonBox::Close)->setText(tr("Cerrar"));
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
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
    AguaraStyle::setRole(summary, AguaraStyle::Muted);
    layout->addWidget(summary);
    layout->addWidget(saveCancel(this, tr("Aplicar")));

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
    summary->setText(list.size() == 1 ? tr("1 artículo") : tr("%1 artículos").arg(list.size()));
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

    QPrinter printer(QPrinter::HighResolution);
    printer.setPageSize(QPageSize(QPageSize::A4));
    printer.setPageMargins(QMarginsF(8, 8, 8, 8), QPageLayout::Millimeter);
    QPrintPreviewDialog preview(&printer, this);
    preview.setWindowTitle(tr("Etiquetas de góndola"));
    connect(&preview, &QPrintPreviewDialog::paintRequested, this, [&labels](QPrinter *p) { LabelSheet::print(p, labels); });
    preview.exec();
    QDialog::accept();
}
