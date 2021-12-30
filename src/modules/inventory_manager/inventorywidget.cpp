#include "inventorywidget.h"
#include "inventoryfactory.h"
#include "ui_inventorywidget.h"
#include <version.h>

#include <QtSql>

inventoryWidget::inventoryWidget(QWidget *parent)
    : AguaraWidget(parent)
    , ui(new Ui::inventoryWidget)
{
    ui->setupUi(this);
    loadTable();
    ModuleMsg msg("", "id", "Inventory Manager - Constructor", "New inventory manager instance created", MsgPriority::PriorityVeryLow);
}

inventoryWidget::~inventoryWidget()
{
    ModuleMsg msg("", "id", "Inventory Manager - Destructor", "Inventory manager instance destroyed", MsgPriority::PriorityVeryLow);

    delete ui;
}

void inventoryWidget::loadTable()
{
    QTableView *table = ui->tableView;
    // Create the data model:
    model = new QSqlTableModel(table);
    model->setEditStrategy(QSqlTableModel::OnManualSubmit);
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
    model->setHeaderData(8, Qt::Horizontal, tr("IVA"));
    model->setHeaderData(9, Qt::Horizontal, tr("F.Compra"));
    model->setHeaderData(10, Qt::Horizontal, tr("F.Modif"));

    // Populate the model:
    if(!model->select()){
        qDebug("Error populating table");
    }

    table->setModel(model);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->verticalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table->resizeColumnsToContents();
    table->setSortingEnabled(true);

}

