#include "inventorywidget.h"
#include "ui_inventorywidget.h"
#include "additemdialog.h"
#include "inventorypages.h"
#include "stockdialogs.h"

#include <QAbstractProxyModel>
#include <QEvent>
#include <QApplication>
#include <QCompleter>
#include <QHeaderView>
#include <QKeySequence>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QShortcut>

inventoryWidget::inventoryWidget(ProductService *products, InventoryService *inventory, UserService *users,
                                 QWidget *parent)
    : AguaraWidget(parent)
    , ui(new Ui::inventoryWidget)
    , products(products)
    , inventory(inventory)
    , users(users)
    , addDialog(new addItemDialog(products, this))
    , editDialog(new addItemDialog(products, this))
    , model(products->createProductTableModel(this))   // refreshes itself on every change
{
    ui->setupUi(this);

    // Connect add and edit buttons
    QObject::connect(ui->itemAddButton, &QPushButton::clicked, this, &inventoryWidget::onItemAddPressed);
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

    // Theme: icons on the actions, a search box with a magnifier
    using AguaraStyle::Color;
    ui->itemAddButton->setIcon(AguaraStyle::icon(QStringLiteral("plus"), AguaraStyle::color(Color::OnAccent)));
    ui->itemEditButton->setIcon(AguaraStyle::icon(QStringLiteral("edit")));
    ui->itemRemoveButton->setIcon(AguaraStyle::icon(QStringLiteral("trash"), AguaraStyle::color(Color::Danger)));
    AguaraStyle::searchField(ui->itemSearch);

    // Header line ("14 artículos · 3 con stock bajo") and buttons that need a selection
    connect(model, &QAbstractItemModel::modelReset, this, &inventoryWidget::updateSummary);
    connect(model, &QAbstractItemModel::rowsInserted, this, &inventoryWidget::updateSummary);
    connect(model, &QAbstractItemModel::rowsRemoved, this, &inventoryWidget::updateSummary);
    connect(ui->tableView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &inventoryWidget::updateActions);
    connect(model, &QAbstractItemModel::modelReset, this, &inventoryWidget::updateActions);
    updateSummary();
    updateActions();

    setUpKeyboard();
    buildSections();
}

/// Artículos (the table) and the rest of the inventory work, in tabs. The
/// buttons of the header act on the articles, so they go with that tab.
void inventoryWidget::buildSections()
{
    using AguaraStyle::Color;
    const auto tabIcon = [](const char *name) {
        return AguaraStyle::icon(QString::fromLatin1(name), AguaraStyle::color(Color::TextMuted));
    };
    ui->pageLayout->removeWidget(ui->body);
    sections = new QTabWidget;
    sections->setObjectName(QStringLiteral("inventorySections"));
    sections->setDocumentMode(true);
    sections->addTab(ui->body, tabIcon("box"), tr("Artículos"));
    auto purchases = new PurchasesPage(products, inventory, users);
    auto suppliers = new SuppliersPage(inventory, users);
    sections->addTab(purchases, tabIcon("cart"), tr("Compras"));
    sections->addTab(suppliers, tabIcon("users"), tr("Proveedores"));
    sections->addTab(new CountsPage(products, inventory, users), tabIcon("list"), tr("Recuentos"));
    ui->pageLayout->addWidget(sections, 1);

    moreButton = new QPushButton(tr("Más"));
    moreButton->setObjectName(QStringLiteral("moreButton"));
    moreMenu = new QMenu(moreButton);
    moreMenu->addAction(AguaraStyle::icon(QStringLiteral("edit")), tr("Ajustar stock..."), this, &inventoryWidget::onAdjustStock);
    moreMenu->addAction(AguaraStyle::icon(QStringLiteral("list")), tr("Historial del artículo..."), this, &inventoryWidget::onHistory);
    moreMenu->addSeparator();
    moreMenu->addAction(AguaraStyle::icon(QStringLiteral("settings")), tr("Cambio de precios..."), this, &inventoryWidget::onPriceChange);
    moreMenu->addAction(AguaraStyle::icon(QStringLiteral("list")), tr("Etiquetas de góndola..."), this, &inventoryWidget::onLabels);
    moreButton->setMenu(moreMenu);
    ui->headerLayout->insertWidget(ui->headerLayout->indexOf(ui->itemRemoveButton), moreButton);

    connect(sections, &QTabWidget::currentChanged, this, [this, purchases, suppliers](int index) {
        for (QWidget *button : { static_cast<QWidget *>(ui->itemAddButton), static_cast<QWidget *>(ui->itemEditButton),
                                 static_cast<QWidget *>(ui->itemRemoveButton), static_cast<QWidget *>(moreButton) })
            button->setVisible(index == 0);
        if (sections->widget(index) == purchases)
            purchases->reload();
        else if (sections->widget(index) == suppliers)
            suppliers->reload();
    });
}

void inventoryWidget::onAdjustStock()
{
    const QString code = selectedCode();
    if (code.isEmpty()) {
        QMessageBox::information(this, tr("Ajustar stock"), tr("Seleccione un artículo."));
        return;
    }
    if (!authorized(QStringLiteral("inventario.ajustar"), tr("Ajustar el stock de %1").arg(code)))
        return;
    if (const auto product = products->product(code))
        AdjustStockDialog(inventory, *product, this).exec();
}

void inventoryWidget::onHistory()
{
    const QString code = selectedCode();
    if (code.isEmpty()) {
        QMessageBox::information(this, tr("Historial"), tr("Seleccione un artículo."));
        return;
    }
    if (const auto product = products->product(code))
        ArticleHistoryDialog(inventory, users, *product, this).exec();
}

void inventoryWidget::onPriceChange()
{
    if (authorized(QStringLiteral("inventario.precios"), tr("Cambiar precios en masa")))
        PriceChangeDialog(products, inventory, this).exec();
}

void inventoryWidget::onLabels()
{
    LabelsDialog(products, inventory, selectedCode(), this).exec();
}

void inventoryWidget::updateSummary()
{
    const int count = model->rowCount();
    int low = 0;
    for (int row = 0; row < count; ++row)
        low += model->index(row, int(ProductColumn::Stock)).data(ProductLowStockRole).toBool() ? 1 : 0;

    ui->summary->setText(count == 1 ? tr("1 artículo") : tr("%1 artículos").arg(count));
    if (low > 0) {
        ui->lowStock->setText(low == 1 ? tr("1 artículo con stock bajo") : tr("%1 artículos con stock bajo").arg(low));
        AguaraStyle::setRole(ui->lowStock, AguaraStyle::Warning);
    } else {
        ui->lowStock->clear();
        AguaraStyle::setRole(ui->lowStock, AguaraStyle::Muted);
    }
    ui->lowStock->setVisible(low > 0);
}

void inventoryWidget::updateActions()
{
    const bool selected = ui->tableView->selectionModel()->hasSelection();
    ui->itemEditButton->setEnabled(selected);
    ui->itemRemoveButton->setEnabled(selected);
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

    ui->itemAddButton->setToolTip(tr("Nuevo artículo (Ins)"));
    ui->itemEditButton->setToolTip(tr("Editar el artículo seleccionado (F2 o Enter)"));
    ui->itemRemoveButton->setToolTip(tr("Dar de baja el artículo seleccionado (Supr)"));

    setFocusProxy(ui->itemSearch);      // opening the page puts the cursor in the search box
}

void inventoryWidget::focusSearch()
{
    ui->itemSearch->setFocus(Qt::ShortcutFocusReason);
    ui->itemSearch->selectAll();
}

/// Ask UserService: the signed-in user, or a supervisor on the spot.
bool inventoryWidget::authorized(const QString &permission, const QString &reason)
{
    return !users || users->authorize(permission, reason, this);
}

void inventoryWidget::onItemAddPressed()
{
    if (authorized(QStringLiteral("inventario.editar"), tr("Nuevo artículo")))
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

    // the main code, another barcode of the article, or a scale's code
    const ScanResult scan = inventory->resolveScan(text);
    const QString wanted = scan.found ? scan.product.code : text;
    int found = -1;
    for (int row = 0; row < model->rowCount() && found < 0; ++row) {
        if (model->index(row, int(ProductColumn::Code)).data().toString().compare(wanted, Qt::CaseInsensitive) == 0)
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

    // fixed, comfortable rows; the description takes the free width
    table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    table->verticalHeader()->setDefaultSectionSize(34);
    QHeaderView *header = table->horizontalHeader();
    header->setSectionResizeMode(QHeaderView::Interactive);
    header->setStretchLastSection(false);
    header->setMinimumSectionSize(60);
    table->resizeColumnsToContents();
    header->setSectionResizeMode(int(ProductColumn::Description), QHeaderView::Stretch);
    // the purchase date says more than the last-modified one; keep the latter out of the way
    table->setColumnHidden(int(ProductColumn::Modified), true);
    table->viewport()->installEventFilter(this);
    fitColumns();
}

/// Narrow screens (1024 px): drop the least needed columns instead of
/// scrolling sideways. Price, cost, IVA and stock always stay.
void inventoryWidget::fitColumns()
{
    const int width = ui->tableView->viewport()->width();
    ui->tableView->setColumnHidden(int(ProductColumn::Purchased), width < 980);
    ui->tableView->setColumnHidden(int(ProductColumn::Brand), width < 860);
}

bool inventoryWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == ui->tableView->viewport() && event->type() == QEvent::Resize)
        fitColumns();
    return AguaraWidget::eventFilter(watched, event);
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
    if (!code.isEmpty() && authorized(QStringLiteral("inventario.editar"), tr("Modificar el artículo %1").arg(code)))
        editDialog->editItem(code);
}

void inventoryWidget::onItemRemovePressed()
{
    const QString code = selectedCode();
    if (code.isEmpty())
        return;

    const int row = ui->tableView->currentIndex().row();
    const QString name = model->index(row, int(ProductColumn::Description)).data().toString();
    QMessageBox box(QMessageBox::Warning, tr("Dar de baja"),
                    tr("¿Dar de baja \"%1\"?").arg(name), QMessageBox::NoButton, this);
    box.setInformativeText(tr("Deja de aparecer en el inventario. Si vuelve a cargarlo con el mismo "
                              "código, se reactiva."));
    QPushButton *confirm = box.addButton(tr("Dar de baja"), QMessageBox::DestructiveRole);
    AguaraStyle::setRole(confirm, AguaraStyle::Danger);
    QPushButton *cancel = box.addButton(tr("Cancelar"), QMessageBox::RejectRole);
    box.setDefaultButton(cancel);           // Enter must not delete by accident
    box.exec();
    if (box.clickedButton() != confirm
        || !authorized(QStringLiteral("inventario.baja"), tr("Dar de baja: %1").arg(name)))
        return;
    if (!products->deactivateProduct(code))
        QMessageBox::warning(this, tr("No se pudo dar de baja"), products->lastError());
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
