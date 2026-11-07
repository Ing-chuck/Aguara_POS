#include "additemdialog.h"
#include "ui_additemdialog.h"
#include "modifytabledialog.h"
#include "stockdialogs.h"

#include <aguarastyle.h>
#include <formcheck.h>

#include <QDate>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QToolButton>
#include <QVBoxLayout>

addItemDialog::addItemDialog(ProductService *products, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::addItemDialog),
    products(products)
{
    ui->setupUi(this);
    setWindowModality(Qt::WindowModal);     // blocks the inventory page, not the whole program

    ui->buttonBox->button(QDialogButtonBox::Save)->setText(tr("Guardar"));
    ui->buttonBox->button(QDialogButtonBox::Save)->setDefault(true);
    ui->buttonBox->button(QDialogButtonBox::Cancel)->setText(tr("Cancelar"));
    for (QToolButton *b : { ui->button_modifyBrandTable, ui->button_modifyTypeTable, ui->button_modifyUnitTable }) {
        b->setIcon(AguaraStyle::icon(QStringLiteral("edit")));
        b->setIconSize(QSize(16, 16));
        b->setFixedSize(32, 32);
    }

    // a code of the shop's own, for what comes without a barcode
    auto generate = new QPushButton(tr("Generar"));
    generate->setObjectName(QStringLiteral("generateCode"));
    generate->setAutoDefault(false);
    generate->setToolTip(tr("Crea un código interno que ningún artículo usa. Sus barras salen en las etiquetas."));
    ui->lineEdit_code->setPlaceholderText(tr("Barras o interno"));
    ui->codeLayout->removeWidget(ui->lineEdit_code);
    auto codeRow = new QHBoxLayout;
    codeRow->setSpacing(6);
    codeRow->addWidget(ui->lineEdit_code, 1);
    codeRow->addWidget(generate);
    ui->codeLayout->addLayout(codeRow);
    connect(generate, &QPushButton::clicked, this, [this]() {
        const QString code = this->products->newInternalCode();
        if (code.isEmpty()) {
            refuse(this->products->lastError(), ui->lineEdit_code);
            return;
        }
        ui->lineEdit_code->setText(code);
        ui->lineEdit_description->setFocus();
    });

    problem = AguaraForm::errorLabel(QStringLiteral("itemError"));
    auto dialogLayout = static_cast<QVBoxLayout *>(layout());
    dialogLayout->insertWidget(dialogLayout->indexOf(ui->buttonBox), problem);

    // Set current date for dateEdit
    ui->dateEdit->setMaximumDate(QDate::currentDate());     // bought: it already happened
    ui->dateEdit->setDate(QDate::currentDate());

    // Setup validators for lineEdits
    // code: a barcode (EAN-13 has 13 digits), a scale's article number or an internal code
    // money: whole guaraníes, "." allowed as thousands separator
    QRegularExpressionValidator *codeVal = new QRegularExpressionValidator(QRegularExpression(R"(^[0-9A-Za-z-]{0,20}$)"), this);
    QRegularExpressionValidator *priceVal = new QRegularExpressionValidator(QRegularExpression(R"(^[\d.]{0,15}$)"), this);
    QRegularExpressionValidator *stockVal = new QRegularExpressionValidator(QRegularExpression(R"(^[\d.,]{0,13}$)"), this);
    ui->lineEdit_code->setValidator(codeVal);
    ui->lineEdit_price->setValidator(priceVal);
    ui->lineEdit_cost->setValidator(priceVal);
    ui->lineEdit_stock->setValidator(stockVal);
    ui->lineEdit_stockmin->setValidator(stockVal);

    reloadLists();

    // Connect buttons to modify comboBox data
    QObject::connect(ui->button_modifyBrandTable, &QToolButton::clicked, this, &addItemDialog::onModifyBrandClicked);
    QObject::connect(ui->button_modifyTypeTable, &QToolButton::clicked, this, &addItemDialog::onModifyTypeClicked);
    QObject::connect(ui->button_modifyUnitTable, &QToolButton::clicked, this, &addItemDialog::onModifyUnitClicked);
    QObject::connect(ui->checkBox_expires, &QCheckBox::toggled, ui->dateEdit_expires, &QWidget::setEnabled);

    // live margin: what the price leaves over the cost
    connect(ui->lineEdit_price, &QLineEdit::textChanged, this, &addItemDialog::updateMargin);
    connect(ui->lineEdit_cost, &QLineEdit::textChanged, this, &addItemDialog::updateMargin);
    updateMargin();
    // OK is wired to accept() in the .ui file; accept() saves.
}

addItemDialog::~addItemDialog()
{
    delete ui;
}

/// Fill a brand/category combo: an empty "none" entry, then the items (id in user data)
void addItemDialog::fillNamedCombo(QComboBox *combo, const QList<NamedItem> &items)
{
    const QVariant current = combo->currentData();
    combo->clear();
    combo->addItem(QString(), qint64(0));
    for (const NamedItem &item : items)
        combo->addItem(item.path.isEmpty() ? item.name : item.path, item.id);
    const int idx = combo->findData(current);
    combo->setCurrentIndex(idx >= 0 ? idx : 0);
}

/// Select an entry by id. A brand/category that has since been removed is
/// not in the list, but the product still has it: show it anyway.
void addItemDialog::selectNamed(QComboBox *combo, qint64 id, const QString &name)
{
    if (id == 0) {
        combo->setCurrentIndex(0);
        return;
    }
    int idx = combo->findData(id);
    if (idx < 0) {
        combo->addItem(name, id);
        idx = combo->count() - 1;
    }
    combo->setCurrentIndex(idx);
}

void addItemDialog::reloadLists()
{
    fillNamedCombo(ui->comboBox_brand, products->brands());
    fillNamedCombo(ui->comboBox_type, products->categories());

    const QVariant currentVat = ui->comboBox_vat->currentData();
    ui->comboBox_vat->clear();
    for (const VatCategory &vat : products->vatCategories())
        ui->comboBox_vat->addItem(vat.name, vat.rate);
    const int idx = ui->comboBox_vat->findData(currentVat);
    ui->comboBox_vat->setCurrentIndex(idx >= 0 ? idx : 0);   // 0 = highest rate (10%)

    const QVariant currentUnit = ui->comboBox_unit->currentData();
    ui->comboBox_unit->clear();
    for (const StockUnit &unit : products->units())
        addUnitChoice(unit);
    ui->comboBox_unit->setCurrentIndex(qMax(ui->comboBox_unit->findData(currentUnit), 0));
}

void addItemDialog::addUnitChoice(const StockUnit &unit)
{
    ui->comboBox_unit->addItem(unit.name, unit.id);
    ui->comboBox_unit->setItemData(ui->comboBox_unit->count() - 1, unit.decimals, Qt::UserRole + 1);
}

void addItemDialog::showModal()
{
    problem->hide();
    ui->dateEdit->setMaximumDate(QDate::currentDate());
    open();
    (editing ? ui->lineEdit_description : ui->lineEdit_code)->setFocus();
}

/// "Margen sobre el precio: 27,6 %" (red when the price is below the cost)
void addItemDialog::updateMargin()
{
    Money price = 0, cost = 0;
    const bool ok = AguaraMoney::parse(ui->lineEdit_price->text(), &price)
                 && AguaraMoney::parse(ui->lineEdit_cost->text().isEmpty() ? QStringLiteral("0")
                                                                          : ui->lineEdit_cost->text(), &cost);
    if (!ok || price <= 0 || cost <= 0) {
        ui->label_margin->setText(QStringLiteral("—"));
        ui->label_margin->setStyleSheet(QString());
        AguaraStyle::setRole(ui->label_margin, AguaraStyle::Muted);
        return;
    }
    const double margin = 100.0 * double(price - cost) / double(price);
    ui->label_margin->setText(tr("%1 %  (%2 Gs. por unidad)")
        .arg(QLocale().toString(margin, 'f', 1), AguaraMoney::format(price - cost)));
    ui->label_margin->setStyleSheet(QStringLiteral("color: %1; font-weight: 600;")
        .arg(AguaraStyle::color(margin < 0 ? AguaraStyle::Color::Danger : AguaraStyle::Color::Success).name()));
}

/// The opening stock is typed here; after that it only changes through
/// movements (Ajustar stock, compras, recuentos), which leave a record.
void addItemDialog::setStockEditable(bool editable)
{
    ui->lineEdit_stock->setReadOnly(!editable);
    ui->lineEdit_stock->setToolTip(editable ? QString() : tr("El stock se cambia con Más > Ajustar stock"));
    // the date belongs to the stock that enters here; otherwise, to its lots
    ui->checkBox_expires->setChecked(false);
    ui->checkBox_expires->setVisible(editable);
    ui->dateEdit_expires->setVisible(editable);
}

/// Empty form for a new article
void addItemDialog::clearForm()
{
    ui->lineEdit_code->clear();
    ui->lineEdit_description->clear();
    ui->comboBox_brand->setCurrentIndex(0);
    ui->comboBox_type->setCurrentIndex(0);
    ui->comboBox_vat->setCurrentIndex(0);       // the highest rate (10 %)
    ui->lineEdit_price->clear();
    ui->lineEdit_cost->clear();
    ui->lineEdit_stock->clear();
    ui->lineEdit_stockmin->clear();
    ui->comboBox_unit->setCurrentIndex(0);
    ui->dateEdit->setDate(QDate::currentDate());
    ui->checkBox_expires->setChecked(false);
    ui->dateEdit_expires->setEnabled(false);
    ui->dateEdit_expires->setDate(QDate::currentDate());
    setStockEditable(true);
}

/// Prepare Dialog to add a new item and show
void addItemDialog::addItem()
{
    editing = false;
    previousCode.clear();
    reloadLists();
    clearForm();
    ui->label_title->setText(tr("Nuevo artículo"));
    setWindowTitle(tr("Nuevo artículo"));
    showModal();
}

void addItemDialog::addForEntry(const QString &code)
{
    addItem();
    setStockEditable(false);
    ui->lineEdit_stock->setToolTip(tr("El stock entra con la cantidad de esta alta"));
    ui->lineEdit_stockmin->setFocus();
    ui->lineEdit_code->setText(code);
    (code.isEmpty() ? ui->lineEdit_code : ui->lineEdit_description)->setFocus();
}

/// Prepare Dialog to edit an existing item and show
void addItemDialog::editItem(const QString &code)
{
    const std::optional<Product> found = products->product(code);
    if (!found) {
        QMessageBox::warning(parentWidget(), tr("Error"), products->lastError());
        return;
    }
    const Product &p = *found;

    editing = true;
    previousCode = p.code;
    ui->label_title->setText(tr("Modificar artículo"));
    setWindowTitle(tr("Modificar artículo"));
    reloadLists();

    ui->lineEdit_code->setText(p.code);
    ui->lineEdit_description->setText(p.description);
    selectNamed(ui->comboBox_brand, p.brandId, p.brandName);
    selectNamed(ui->comboBox_type, p.categoryId, p.categoryName);
    ui->lineEdit_price->setText(AguaraMoney::format(p.price));
    ui->lineEdit_cost->setText(AguaraMoney::format(p.cost));
    ui->lineEdit_stock->setText(AguaraQuantity::format(p.stock, p.unit.decimals));
    ui->lineEdit_stockmin->setText(AguaraQuantity::format(p.stockMin, p.unit.decimals));
    if (ui->comboBox_unit->findData(p.unit.id) < 0)
        addUnitChoice(p.unit);      // a unit taken off the list: saving must not change it to another
    ui->comboBox_unit->setCurrentIndex(qMax(ui->comboBox_unit->findData(p.unit.id), 0));
    setStockEditable(false);
    const int vatIdx = ui->comboBox_vat->findData(p.vatRate);
    ui->comboBox_vat->setCurrentIndex(vatIdx >= 0 ? vatIdx : 0);
    ui->dateEdit->setDate(p.purchased.isValid() ? p.purchased : QDate::currentDate());

    showModal();
}

void addItemDialog::refuse(const QString &text, QWidget *field)
{
    AguaraForm::showError(problem, text, field);
}

/// Read the form into *product; shows a message and returns false if invalid
bool addItemDialog::readInput(Product *product)
{
    problem->hide();
    Product p;
    p.code        = ui->lineEdit_code->text().trimmed();
    p.description = ui->lineEdit_description->text().trimmed();
    p.brandId     = ui->comboBox_brand->currentData().toLongLong();
    p.brandName   = ui->comboBox_brand->currentText();
    p.categoryId  = ui->comboBox_type->currentData().toLongLong();
    p.categoryName = ui->comboBox_type->currentText();
    p.vatRate     = ui->comboBox_vat->currentData().toInt();
    p.purchased   = ui->dateEdit->date();
    p.stockExpires = ui->checkBox_expires->isChecked() ? ui->dateEdit_expires->date() : QDate();
    p.unit.id     = ui->comboBox_unit->currentData().toLongLong();
    const int decimals = ui->comboBox_unit->currentData(Qt::UserRole + 1).toInt();

    if (p.code.isEmpty()) {
        refuse(tr("Escriba el código del artículo."), ui->lineEdit_code);
        return false;
    }
    if (p.description.isEmpty()) {
        refuse(tr("Escriba la descripción del artículo."), ui->lineEdit_description);
        return false;
    }

    const QString stockText = ui->lineEdit_stock->text().isEmpty() ? QStringLiteral("0") : ui->lineEdit_stock->text();
    const QString minimumText = ui->lineEdit_stockmin->text().isEmpty() ? QStringLiteral("0") : ui->lineEdit_stockmin->text();
    if (!AguaraQuantity::parse(stockText, &p.stock)) {
        refuse(tr("El stock debe ser una cantidad, por ejemplo 24 o 2,750."), ui->lineEdit_stock);
        return false;
    }
    if (!AguaraQuantity::parse(minimumText, &p.stockMin)) {
        refuse(tr("El stock mínimo debe ser una cantidad, por ejemplo 24 o 2,750."), ui->lineEdit_stockmin);
        return false;
    }
    // the stock of an article that exists doesn't change here
    if (!ui->lineEdit_stock->isReadOnly() && !AguaraForm::fitsUnit(p.stock, decimals)) {
        refuse(tr("Esta unidad de venta no admite tantos decimales."), ui->lineEdit_stock);
        return false;
    }
    if (p.stockExpires.isValid() && p.stock <= 0) {
        refuse(tr("La fecha de vencimiento es del stock: escriba cuánto hay."), ui->lineEdit_stock);
        return false;
    }
    if (!AguaraForm::fitsUnit(p.stockMin, decimals)) {
        refuse(tr("Esta unidad de venta no admite tantos decimales."), ui->lineEdit_stockmin);
        return false;
    }

    const QString priceText = ui->lineEdit_price->text();
    const QString costText  = ui->lineEdit_cost->text();
    if (priceText.trimmed().isEmpty()) {
        refuse(tr("Escriba el precio de venta."), ui->lineEdit_price);
        return false;
    }
    if (!AguaraMoney::parse(priceText, &p.price)) {
        refuse(tr("El precio \"%1\" no es un monto válido.").arg(priceText), ui->lineEdit_price);
        return false;
    }
    if (p.price <= 0) {     // it would go through the till for free
        refuse(tr("El precio de venta debe ser mayor que cero."), ui->lineEdit_price);
        return false;
    }
    if (!AguaraMoney::parse(costText.isEmpty() ? QStringLiteral("0") : costText, &p.cost)) {
        refuse(tr("El costo \"%1\" no es un monto válido.").arg(costText), ui->lineEdit_cost);
        return false;
    }

    *product = p;
    return true;
}

/// Create and show right instance of modifyTableDialog
void addItemDialog::onModifyBrandClicked()
{
    auto dialog = new modifyTableDialog(products, modifyTableDialog::Brands, this);
    connect(dialog, &modifyTableDialog::listChanged, this, &addItemDialog::reloadLists);
    dialog->show();
}

void addItemDialog::onModifyTypeClicked()
{
    auto dialog = new modifyTableDialog(products, modifyTableDialog::Categories, this);
    connect(dialog, &modifyTableDialog::listChanged, this, &addItemDialog::reloadLists);
    dialog->show();
}

void addItemDialog::onModifyUnitClicked()
{
    auto dialog = new UnitsDialog(products, this);
    connect(dialog, &UnitsDialog::listChanged, this, &addItemDialog::reloadLists);
    dialog->show();
}

void addItemDialog::reject()
{
    QDialog::reject();
}

/// OK: save, and close only if saving worked
void addItemDialog::accept()
{
    Product p;
    if (!readInput(&p))
        return;

    // Adding with a code that already exists would overwrite that product
    if (!editing) {
        const std::optional<Product> existing = products->product(p.code);
        if (existing && existing->active) {
            const auto btn = QMessageBox::question(this, tr("Código existente"),
                tr("El código %1 ya pertenece a \"%2\".\n¿Reemplazar ese artículo?")
                    .arg(p.code, existing->description),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (btn != QMessageBox::Yes)
                return;
        }
    }

    if (!products->saveProduct(p, editing ? previousCode : QString())) {
        refuse(products->lastError());
        return;
    }

    QDialog::accept();
    emit itemAddedEdited(p.code);
}
