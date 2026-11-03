#include "inventorywidget.h"
#include "ui_inventorywidget.h"
#include "additemdialog.h"

#include <QAbstractProxyModel>
#include <QCompleter>
#include <QHeaderView>
#include <QMessageBox>

inventoryWidget::inventoryWidget(ProductService *products, QWidget *parent)
    : AguaraWidget(parent)
    , ui(new Ui::inventoryWidget)
    , products(products)
    , addDialog(new addItemDialog(products, this))
    , editDialog(new addItemDialog(products, this))
    , model(products->createProductTableModel(this))   // refreshes itself on every change
{
    ui->setupUi(this);

    // Connect add and edit buttons
    QObject::connect(ui->itemAddButton, &QPushButton::clicked, addDialog, &addItemDialog::addItem);
    QObject::connect(ui->itemEditButton, &QPushButton::clicked, this, &inventoryWidget::onItemEditPressed);

    loadTable();

    // Search completer
    searchCompleter = new QCompleter(model, this);
    //searchCompleter->setCompletionMode(QCompleter::InlineCompletion);
    searchCompleter->setCompletionColumn(int(ProductColumn::Description));
    ui->itemSearch->setCompleter(searchCompleter);

    connect(searchCompleter, QOverload<const QModelIndex &>::of(&QCompleter::highlighted), this, &inventoryWidget::onCompleterAccepted);

    // Select the saved item after an add or edit
    QObject::connect(addDialog, &addItemDialog::itemAddedEdited, this, &inventoryWidget::onItemAddedEdited);
    QObject::connect(editDialog, &addItemDialog::itemAddedEdited, this, &inventoryWidget::onItemAddedEdited);

    // Conect remove button to onItemRemovePressed
    QObject::connect(ui->itemRemoveButton, &QPushButton::clicked, this, &inventoryWidget::onItemRemovePressed);
}

inventoryWidget::~inventoryWidget()
{
    delete ui;
}

void inventoryWidget::loadTable()
{
    QTableView *table = ui->tableView;
    table->setModel(model);     // columns already in display order (ProductColumn)
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);  // edit through the dialog
    table->verticalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    table->resizeColumnsToContents();
}

/// Code of the selected product, or an empty string
QString inventoryWidget::selectedCode() const
{
    if (!ui->tableView->selectionModel()->hasSelection())
        return QString();
    const int row = ui->tableView->currentIndex().row();
    return model->index(row, int(ProductColumn::Code)).data().toString();
}

/// Select the added/edited row (the model has already refreshed)
void inventoryWidget::onItemAddedEdited(QString id)
{
    for (int row = 0; row < model->rowCount(); row++) {
        if (model->index(row, int(ProductColumn::Code)).data().toString() == id) {
            ui->tableView->selectRow(row);
            ui->tableView->scrollTo(model->index(row, 0));
            break;
        }
    }
}

void inventoryWidget::onItemEditPressed()
{
    const QString code = selectedCode();
    if (!code.isEmpty())
        editDialog->editItem(code);
}

void inventoryWidget::onItemRemovePressed()
{
    const QString code = selectedCode();
    if (code.isEmpty())
        return;

    const int row = ui->tableView->currentIndex().row();
    const QString name = model->index(row, int(ProductColumn::Description)).data().toString();
    QString warningTitle = tr("ATENCION");
    QString warningBody = tr("¿Esta seguro que desea dar de baja el articulo\n\"%1\"?\n\n"
                             "Dejara de aparecer en el inventario.").arg(name);
    QMessageBox::StandardButton btn = QMessageBox::warning(this, warningTitle, warningBody, QMessageBox::Ok|QMessageBox::Cancel, QMessageBox::Cancel);
    if (btn == QMessageBox::Ok && !products->deactivateProduct(code))
        QMessageBox::warning(this, tr("Error"), products->lastError());
}

void inventoryWidget::onCompleterAccepted(const QModelIndex &index)
{
    // the completer reports rows of its own (filtered) model: map back to ours
    auto proxy = qobject_cast<QAbstractProxyModel *>(searchCompleter->completionModel());
    const QModelIndex source = proxy ? proxy->mapToSource(index) : index;
    if (source.isValid()) {
        ui->tableView->selectRow(source.row());
        ui->tableView->scrollTo(source);
    }
}
