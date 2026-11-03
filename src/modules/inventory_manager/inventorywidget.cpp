#include "inventorywidget.h"
#include "ui_inventorywidget.h"
#include "additemdialog.h"

#include <QAbstractProxyModel>
#include <QApplication>
#include <QCompleter>
#include <QHeaderView>
#include <QKeySequence>
#include <QMessageBox>
#include <QShortcut>

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

    setUpKeyboard();
}

/// Everything works without the mouse, and with a barcode scanner (which
/// types the code and presses Enter):
///   Ctrl+F / F3   search box          Enter in the search box: find it
///   Ins / Ctrl+N  new article         F2 / Enter on the table: edit
///   Del           remove (deactivate) Esc in the search box: clear it
void inventoryWidget::setUpKeyboard()
{
    auto shortcut = [this](const QKeySequence &keys, void (inventoryWidget::*slot)()) {
        auto s = new QShortcut(keys, this);
        s->setContext(Qt::WidgetWithChildrenShortcut);      // only while this page is shown
        connect(s, &QShortcut::activated, this, slot);
    };
    shortcut(QKeySequence(Qt::CTRL + Qt::Key_F), &inventoryWidget::focusSearch);
    shortcut(QKeySequence(Qt::Key_F3), &inventoryWidget::focusSearch);
    shortcut(QKeySequence(Qt::Key_Insert), &inventoryWidget::onItemAddPressed);
    shortcut(QKeySequence(Qt::CTRL + Qt::Key_N), &inventoryWidget::onItemAddPressed);
    shortcut(QKeySequence(Qt::Key_F2), &inventoryWidget::onItemEditPressed);

    // Del only from the table: in the search box it deletes text
    auto remove = new QShortcut(QKeySequence(Qt::Key_Delete), ui->tableView);
    remove->setContext(Qt::WidgetShortcut);
    connect(remove, &QShortcut::activated, this, &inventoryWidget::onItemRemovePressed);

    connect(ui->tableView, &QAbstractItemView::activated, this, &inventoryWidget::onItemEditPressed);
    connect(ui->itemSearch, &QLineEdit::returnPressed, this, &inventoryWidget::onSearchEntered);
    connect(ui->itemSearchButton, &QToolButton::clicked, this, &inventoryWidget::onSearchEntered);
    ui->itemSearch->setClearButtonEnabled(true);
    ui->itemSearch->setPlaceholderText(tr("Buscar por código o descripción (Ctrl+F)"));

    ui->itemAddButton->setToolTip(tr("Nuevo artículo (Ins)"));
    ui->itemEditButton->setToolTip(tr("Editar el artículo seleccionado (F2 o Enter)"));
    ui->itemRemoveButton->setToolTip(tr("Dar de baja el artículo seleccionado (Supr)"));
    ui->itemSearchButton->setToolTip(tr("Buscar (Enter)"));

    setFocusProxy(ui->itemSearch);      // opening the page puts the cursor in the search box
}

void inventoryWidget::focusSearch()
{
    ui->itemSearch->setFocus(Qt::ShortcutFocusReason);
    ui->itemSearch->selectAll();
}

void inventoryWidget::onItemAddPressed()
{
    addDialog->addItem();
}

/// Enter in the search box: an exact code (what a scanner sends) selects
/// that article; otherwise the first article whose description contains the
/// text. The text stays selected so the next scan replaces it.
void inventoryWidget::onSearchEntered()
{
    const QString text = ui->itemSearch->text().trimmed();
    if (text.isEmpty())
        return;

    int found = -1;
    for (int row = 0; row < model->rowCount() && found < 0; ++row) {
        if (model->index(row, int(ProductColumn::Code)).data().toString().compare(text, Qt::CaseInsensitive) == 0)
            found = row;
    }
    for (int row = 0; row < model->rowCount() && found < 0; ++row) {
        if (model->index(row, int(ProductColumn::Description)).data().toString().contains(text, Qt::CaseInsensitive))
            found = row;
    }

    if (found < 0) {
        QApplication::beep();
        emit notify(ModuleMsg(QString(), QStringLiteral("search"), tr("Sin resultados"),
                              tr("No hay artículos con el código o la descripción \"%1\".").arg(text),
                              MsgPriority::PriorityLow, 5));
    } else {
        ui->tableView->selectRow(found);
        ui->tableView->scrollTo(model->index(found, 0));
    }
    ui->itemSearch->selectAll();
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
