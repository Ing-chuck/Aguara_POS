#include "additemdialog.h"
#include "ui_additemdialog.h"
#include "modifytabledialog.h"

#include <QDate>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>

addItemDialog::addItemDialog(ProductService *products, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::addItemDialog),
    products(products)
{
    setWindowFlag(Qt::FramelessWindowHint);
    ui->setupUi(this);

    // Set current date for dateEdit
    ui->dateEdit->setDate(QDate::currentDate());

    // Setup validators for lineEdits
    // code: digits, up to 14 (GTIN-14; EAN-13 barcodes have 13)
    // money: whole guaraníes, "." allowed as thousands separator
    QRegularExpressionValidator *codeVal = new QRegularExpressionValidator(QRegularExpression(R"(^\d{0,14}$)"), this);
    QRegularExpressionValidator *priceVal = new QRegularExpressionValidator(QRegularExpression(R"(^[\d.]{0,15}$)"), this);
    QRegularExpressionValidator *stockVal = new QRegularExpressionValidator(QRegularExpression(R"(^\d{0,9}$)"), this);
    ui->lineEdit_code->setValidator(codeVal);
    ui->lineEdit_price->setValidator(priceVal);
    ui->lineEdit_cost->setValidator(priceVal);
    ui->lineEdit_stock->setValidator(stockVal);
    ui->lineEdit_stockmin->setValidator(stockVal);

    reloadLists();

    // Connect buttons to modify comboBox data
    QObject::connect(ui->button_modifyBrandTable, &QToolButton::clicked, this, &addItemDialog::onModifyBrandClicked);
    QObject::connect(ui->button_modifyTypeTable, &QToolButton::clicked, this, &addItemDialog::onModifyTypeClicked);
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
        combo->addItem(item.name, item.id);
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
}

void addItemDialog::showModal()
{
    this->parentWidget()->setEnabled(false);
    setEnabled(true);
    show();
}

/// Prepare Dialog to add a new item and show
void addItemDialog::addItem()
{
    editing = false;
    previousCode.clear();
    reloadLists();
    showModal();
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
    ui->label_title->setText(tr("Modificar Articulo"));
    reloadLists();

    ui->lineEdit_code->setText(p.code);
    ui->lineEdit_description->setText(p.description);
    selectNamed(ui->comboBox_brand, p.brandId, p.brandName);
    selectNamed(ui->comboBox_type, p.categoryId, p.categoryName);
    ui->lineEdit_price->setText(AguaraMoney::format(p.price));
    ui->lineEdit_cost->setText(AguaraMoney::format(p.cost));
    ui->lineEdit_stock->setText(QString::number(p.stock));
    ui->lineEdit_stockmin->setText(QString::number(p.stockMin));
    const int vatIdx = ui->comboBox_vat->findData(p.vatRate);
    ui->comboBox_vat->setCurrentIndex(vatIdx >= 0 ? vatIdx : 0);
    ui->dateEdit->setDate(p.purchased.isValid() ? p.purchased : QDate::currentDate());

    showModal();
}

/// Read the form into *product; shows a message and returns false if invalid
bool addItemDialog::readInput(Product *product)
{
    Product p;
    p.code        = ui->lineEdit_code->text().trimmed();
    p.description = ui->lineEdit_description->text().trimmed();
    p.brandId     = ui->comboBox_brand->currentData().toLongLong();
    p.brandName   = ui->comboBox_brand->currentText();
    p.categoryId  = ui->comboBox_type->currentData().toLongLong();
    p.categoryName = ui->comboBox_type->currentText();
    p.vatRate     = ui->comboBox_vat->currentData().toInt();
    p.stock       = ui->lineEdit_stock->text().toInt();
    p.stockMin    = ui->lineEdit_stockmin->text().toInt();
    p.purchased   = ui->dateEdit->date();

    const QString priceText = ui->lineEdit_price->text();
    const QString costText  = ui->lineEdit_cost->text();
    if (!AguaraMoney::parse(priceText.isEmpty() ? QStringLiteral("0") : priceText, &p.price)) {
        QMessageBox::warning(this, tr("Precio invalido"), tr("El precio \"%1\" no es un monto valido.").arg(priceText));
        return false;
    }
    if (!AguaraMoney::parse(costText.isEmpty() ? QStringLiteral("0") : costText, &p.cost)) {
        QMessageBox::warning(this, tr("Costo invalido"), tr("El costo \"%1\" no es un monto valido.").arg(costText));
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

void addItemDialog::reject()
{
    this->parentWidget()->setEnabled(true);
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
            const auto btn = QMessageBox::question(this, tr("Codigo existente"),
                tr("El codigo %1 ya pertenece a \"%2\".\n¿Reemplazar ese articulo?")
                    .arg(p.code, existing->description),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (btn != QMessageBox::Yes)
                return;
        }
    }

    if (!products->saveProduct(p, editing ? previousCode : QString())) {
        QMessageBox::warning(this, tr("No se pudo guardar"), products->lastError());
        return;
    }

    this->parentWidget()->setEnabled(true);
    QDialog::accept();
    emit itemAddedEdited(p.code);
}
