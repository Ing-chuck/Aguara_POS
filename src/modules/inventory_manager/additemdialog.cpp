#include "additemdialog.h"
#include "ui_additemdialog.h"
#include "modifytabledialog.h"

#include <QtSql>
#include <QDate>
#include <QTableView>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>

addItemDialog::addItemDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::addItemDialog),
    brandModel(new QSqlQueryModel(this)),
    typeModel(new QSqlQueryModel(this)),
    vatModel(new QSqlQueryModel(this))
{
    setWindowFlag(Qt::FramelessWindowHint);
    //setAttribute(Qt::WA_DeleteOnClose);
    //parent->setEnabled(false);
    //setEnabled(true);
    //setWindowModality(Qt::WindowModal);
    ui->setupUi(this);

    // Set current date for dateEdit
    ui->dateEdit->setDate(QDate::currentDate());

    // Stup validators for lineEdits
    QRegularExpressionValidator *codeVal = new QRegularExpressionValidator(QRegularExpression(R"(^\d{0,12}$)"), this);
    QRegularExpressionValidator *priceVal = new QRegularExpressionValidator(QRegularExpression(R"(^\d+\.?\d{0,2}$)"), this);
    QRegularExpressionValidator *stockVal = new QRegularExpressionValidator(QRegularExpression(R"(^\d{0,20}$)"), this);
    ui->lineEdit_code->setValidator(codeVal);
    ui->lineEdit_price->setValidator(priceVal);
    ui->lineEdit_cost->setValidator(priceVal);
    ui->lineEdit_stock->setValidator(stockVal);
    ui->lineEdit_stockmin->setValidator(stockVal);


    // Create query models for brand and type comboboxes
    /// TODO
    /// * make db column name configurable
    brandModel->setQuery("select brand_name From brands ORDER BY brand_name");
    typeModel->setQuery("select type From busines_types ORDER BY type");
    vatModel->setQuery("select vat_value From vats ORDER BY vat_value DESC");

    // Set the comboBoxes with the respective models
    ui->comboBox_brand->setModel(brandModel);
    ui->comboBox_type->setModel(typeModel);
    ui->comboBox_vat->setModel(vatModel);

    // Connect buttons to modify comboBox data
    QObject::connect(ui->button_modifyBrandTable, &QPushButton::clicked, this, &addItemDialog::onModifyBrandClicked);
    QObject::connect(ui->button_modifyTypeTable, &QPushButton::clicked, this, &addItemDialog::onModifyTypeClicked);

    // Connect button to save the new item
    QObject::connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &addItemDialog::onSaveClicked);
}

addItemDialog::~addItemDialog()
{
    delete ui;
}

/// Prepare Dialog to add a new item and show
void addItemDialog::addItem()
{
    this->parentWidget()->setEnabled(false);
    setEnabled(true);
    editing = false;
    show();
}

/// Prepare Dialog to edit an existing item and show
void addItemDialog::editItem(QTableView *view)
{
    if(view->selectionModel()->hasSelection()) {
        this->parentWidget()->setEnabled(false);
        setEnabled(true);
        editing = true;
        ui->label_title->setText("Modificar Articulo");

        tableModel = qobject_cast<QSqlTableModel *>(view->model());
        selectedRow = view->currentIndex().row();

        getDataFromSelection();
        show();
    }
}

/// Fill internal variables
/// with data from selected row in the table
void addItemDialog::getDataFromSelection()
{
    itemCode = tableModel->data(tableModel->index(selectedRow, 0)).toString();
    itemDescription = tableModel->data(tableModel->index(selectedRow, 1)).toString();
    itemBrand = tableModel->data(tableModel->index(selectedRow, 2)).toString();
    itemType = tableModel->data(tableModel->index(selectedRow, 3)).toString();
    itemPrice = tableModel->data(tableModel->index(selectedRow, 4)).toString();
    itemCost = tableModel->data(tableModel->index(selectedRow, 5)).toString();
    itemStock = tableModel->data(tableModel->index(selectedRow, 6)).toString();
    itemStockMin = tableModel->data(tableModel->index(selectedRow, 7)).toString();
    itemVat = tableModel->data(tableModel->index(selectedRow, 8)).toString();
    itemBuyDate = tableModel->data(tableModel->index(selectedRow, 9)).toString();
    itemModDate = QDate::currentDate().toString(Qt::ISODate);

    // Set lineEdits to match the data
    ui->lineEdit_code->setText(itemCode);
    ui->lineEdit_description->setText(itemDescription);
    ui->comboBox_brand->setCurrentText(itemBrand);
    ui->comboBox_type->setCurrentText(itemType);
    ui->lineEdit_price->setText(itemPrice);
    ui->lineEdit_cost->setText(itemCost);
    ui->lineEdit_stock->setText(itemStock);
    ui->lineEdit_stockmin->setText(itemStockMin);
    ui->comboBox_vat->setCurrentText(itemVat);
    ui->dateEdit->setDate(QDate::fromString(itemBuyDate, Qt::ISODate));
    //ui->dateEdit = new QDateEdit(QDate::fromString(itemBuyDate), ui->dateEdit->parentWidget());
}

/// Fill internal variables
/// with user input from form
void addItemDialog::getDataFromInput()
{
    itemCode = ui->lineEdit_code->text();
    itemDescription = ui->lineEdit_description->text();
    itemBrand = ui->comboBox_brand->currentText();
    itemType = ui->comboBox_type->currentText();
    itemPrice = ui->lineEdit_price->text();
    itemCost = ui->lineEdit_cost->text();
    itemStock = ui->lineEdit_stock->text();
    itemStockMin = ui->lineEdit_stockmin->text();
    itemVat = ui->comboBox_vat->currentText();
    itemBuyDate = ui->dateEdit->text();
    itemModDate = QDate::currentDate().toString(Qt::ISODate);
}

bool addItemDialog::checkItemCodeChanged()
{
    return itemCode == ui->lineEdit_code->text();
}

/// Create and show right instance of modifyTableDialog
/// make sure not to leak memory
void addItemDialog::onModifyBrandClicked()
{
    modifyDialog = new modifyTableDialog(brandModel, true, this);
    modifyDialog->show();
}

/// Create and show right instance of modifyTableDialog
/// make sure not to leak memory
void addItemDialog::onModifyTypeClicked()
{
    modifyDialog = new modifyTableDialog(typeModel, false, this);
    modifyDialog->show();
}

/// Take validated user input data and save to database
void addItemDialog::onSaveClicked()
{

    if(editing && !checkItemCodeChanged()) {
        // Remove previous entry for this item
        if(tableModel != nullptr)
            tableModel->removeRows(selectedRow, 1);
    }

    // Get item data
    getDataFromInput();

    // Make insert query
    QString sql = QString("INSERT OR REPLACE INTO articles VALUES('%1',' %2', '%3', '%4', '%5', '%6', '%7', '%8', '%9', '%10', '%11')")
                  .arg(itemCode, itemDescription, itemBrand, itemType, itemPrice, itemCost, itemStock, itemStockMin, itemVat, itemBuyDate, itemModDate);

    QSqlQuery query;
    query.exec(sql);

    if(query.lastError().isValid()) {
        qDebug() << __FUNCTION__ << ": " << query.lastError();
    }
    else {
        emit itemAddedEdited(itemCode);
    }
}

void addItemDialog::reject()
{
    this->parentWidget()->setEnabled(true);
    QDialog::reject();
}

void addItemDialog::accept()
{
    this->parentWidget()->setEnabled(true);
    QDialog::accept();
}
