#include "inventorywidget.h"
#include "inventoryfactory.h"
#include "ui_inventorywidget.h"
#include "additemdialog.h"
#include "customsqlmodel.h"
#include <version.h>

#include <QtSql>
#include <QCompleter>
#include <QMessageBox>

inventoryWidget::inventoryWidget(QWidget *parent)
    : AguaraWidget(parent)
    , ui(new Ui::inventoryWidget)
    , addDialog(new addItemDialog(this))
    , editDialog(new addItemDialog(this))
    , model(new CustomSqlModel(this))
{
    ui->setupUi(this);

    // Connect add and edit buttons
    QObject::connect(ui->itemAddButton, &QPushButton::clicked, addDialog, &addItemDialog::addItem);
    QObject::connect(ui->itemEditButton, &QPushButton::clicked, this, [=](){editDialog->editItem(ui->tableView);});

    loadTable();
    ModuleMsg msg("", "id", "Inventory Manager - Constructor", "New inventory manager instance created", MsgPriority::PriorityVeryLow);

    // Search completer
    searchCompleter = new QCompleter(model, this);
    //searchCompleter->setCompletionMode(QCompleter::InlineCompletion);
    searchCompleter->setCompletionColumn(1);
    ui->itemSearch->setCompleter(searchCompleter);

    connect(searchCompleter, QOverload<const QModelIndex &>::of(&QCompleter::highlighted), this, &inventoryWidget::onCompleterAccepted);

    // Connect addDialog's accepted to this onItemAddedEdited
    QObject::connect(addDialog, &addItemDialog::itemAddedEdited, this, &inventoryWidget::onItemAddedEdited);
    QObject::connect(editDialog, &addItemDialog::itemAddedEdited, this, &inventoryWidget::onItemAddedEdited);

    // Conect remove button to onItemRemovePressed
    QObject::connect(ui->itemRemoveButton, &QPushButton::clicked, this, &inventoryWidget::onItemRemovePressed);
}

inventoryWidget::~inventoryWidget()
{
    ModuleMsg msg("", "id", "Inventory Manager - Destructor", "Inventory manager instance destroyed", MsgPriority::PriorityVeryLow);

    delete ui;
}



void inventoryWidget::loadTable()
{
    QTableView *table = ui->tableView;
    // Configure the data model:
    model->setEditStrategy(QSqlTableModel::OnRowChange);
    model->setTable("articles");

    // Set the localized header captions:
    model->setHeaderData(0, Qt::Horizontal, tr("Codigo"));
    model->setHeaderData(1, Qt::Horizontal, tr("Descripcion"));
    model->setHeaderData(2, Qt::Horizontal, tr("Marca"));
    model->setHeaderData(3, Qt::Horizontal, tr("Rubro"));
    model->setHeaderData(4, Qt::Horizontal, tr("PrecioVenta"));
    model->setHeaderData(5, Qt::Horizontal, tr("Costo"));
    model->setHeaderData(6, Qt::Horizontal, tr("Stock"));
    model->setHeaderData(7, Qt::Horizontal, tr("StockMin"));
    model->setHeaderData(8, Qt::Horizontal, tr("IVA %"));
    model->setHeaderData(9, Qt::Horizontal, tr("F.Compra"));
    model->setHeaderData(10, Qt::Horizontal, tr("F.Modif"));

    // Populate the model:
    if(!refreshModel()){
        qDebug() << "Error populating table";
        qDebug() << model->query().lastError();
    }

    table->setModel(model);
    table->verticalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table->resizeColumnsToContents();

}

/// Refresh model data
/// return true on success
/// false on error
bool inventoryWidget::refreshModel()
{
    if(!model)
        return false;

    model->setSort(0, Qt::AscendingOrder);
    return model->select();
}

/// Refresh the table data and select the new row
void inventoryWidget::onItemAddedEdited(QString id)
{
    qDebug() << __FUNCTION__ << "(" << id << ")";

    if(!refreshModel()){
        qDebug() << "Error populating table";
        qDebug() << model->query().lastError();
    }
    else {
        // Search for inserted item and select it
        for(int row = 0; row < model->rowCount(); row++)
        {
            QString code = model->data(model->index(row,0)).toString();
            if(code == id) {
                ui->tableView->setCurrentIndex(model->index(row, 0));
                break;
            }
        }
    }
}

void inventoryWidget::onItemRemovePressed()
{
    if(ui->tableView->selectionModel()->hasSelection())
    {
        int row = ui->tableView->currentIndex().row();
        QString warningTitle = tr("ATENCION");
        QString name = model->data(model->index(row,1)).toString();
        QString warningBody = QString("¿Esta seguro que desea REMOVER el artiulo\n\"%1\"?\n\nEsta accion es permanente").arg(name);
        QMessageBox::StandardButton btn = QMessageBox::warning(this, warningTitle, warningBody, QMessageBox::Ok|QMessageBox::Cancel, QMessageBox::Cancel);
        if(btn == QMessageBox::Ok)
        {
            //deleteRow(model->data(indx).toString());
            model->removeRows(row, 1);
            refreshModel();
        }
    }
}

void inventoryWidget::onCompleterAccepted(const QModelIndex &index)
{
    ui->tableView->selectionModel()->select(index,QItemSelectionModel::Select);
}
