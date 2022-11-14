#include "inventorywidget.h"
#include "inventoryfactory.h"
#include "ui_inventorywidget.h"
#include "additemdialog.h"
#include <version.h>

#include <QtSql>
#include <QCompleter>
#include <QMessageBox>

inventoryWidget::inventoryWidget(QWidget *parent)
    : AguaraWidget(parent)
    , ui(new Ui::inventoryWidget)
    , addDialog(new addItemDialog(this))
    , editDialog(new addItemDialog(this))
    , model(new InventorySqlModel(this))
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
    model->setTable("productos");
    model->setFilter("productos.es_activo = true");

    // Populate the model:
    if(!refreshModel()){
        qDebug() << "Error populating table";
        qDebug() << model->query().lastError();
    }

    table->setModel(model);
    hideUnusedColumns();
    table->verticalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    table->resizeColumnsToContents();

    // move columns to the preffered location
    table->horizontalHeader()->moveSection(8, 12);  // es_activo
    table->horizontalHeader()->moveSection(7, 11);  // FMod
    table->horizontalHeader()->moveSection(6, 10);  // FCom
    table->horizontalHeader()->moveSection(5, 9);   // Stock Min
    table->horizontalHeader()->moveSection(4, 8);   // Stock
    table->horizontalHeader()->moveSection(6, 7);   // IVA
    table->horizontalHeader()->moveSection(3, 6);   // Costo
    table->horizontalHeader()->moveSection(2, 5);   // Precio


}

/// Hide columns that are not needed from the model
void inventoryWidget::hideUnusedColumns()
{
    // colums
    // "es_activo"
    QTableView *table = ui->tableView;
    QVector<QString> colsToHide = {"es_activo"};
    for (auto &field : colsToHide) {
        table->hideColumn(model->fieldIndex(field));
    }
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
            //TODO
            //set this item's es_activo to false in the database
            model->removeRows(row, 1);
            refreshModel();
        }
    }
}

void inventoryWidget::onCompleterAccepted(const QModelIndex &index)
{
    ui->tableView->selectionModel()->select(index,QItemSelectionModel::Select);
}
