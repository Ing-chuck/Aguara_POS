#include "modifytabledialog.h"
#include "ui_modifytabledialog.h"

#include <QAbstractProxyModel>
#include <QCompleter>
#include <QHeaderView>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardItemModel>

modifyTableDialog::modifyTableDialog(ProductService *products, List list, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::modifyTableDialog),
    model(new QStandardItemModel(this)),
    products(products),
    list(list)
{
    // Window configuration
    setWindowFlag(Qt::FramelessWindowHint);
    setAttribute(Qt::WA_DeleteOnClose);
    parent->setEnabled(false);
    setEnabled(true);

    ui->setupUi(this);

    // Make line edits write in all caps
    QObject::connect(ui->lineEdit_search, &QLineEdit::textEdited, this, [this](const QString &text){ui->lineEdit_search->setText(text.toUpper());});
    QObject::connect(ui->lineEdit_add, &QLineEdit::textEdited, this, [this](const QString &text){ui->lineEdit_add->setText(text.toUpper());});

    if (list == Brands) {
        ui->label_title->setText(tr("Actualizar Tabla de Marcas"));
        model->setHorizontalHeaderLabels({ tr("Marcas") });
    }
    else {
        ui->label_title->setText(tr("Actualizar Tabla de Rubros"));
        model->setHorizontalHeaderLabels({ tr("Rubros") });
    }
    reload();

    ui->tableView->setModel(model);
    ui->tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->tableView->verticalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    // Search completer
    searchCompleter = new QCompleter(model, this);
    searchCompleter->setCaseSensitivity(Qt::CaseInsensitive);
    searchCompleter->setCompletionMode(QCompleter::InlineCompletion);
    ui->lineEdit_search->setCompleter(searchCompleter);

    connect(searchCompleter, QOverload<const QModelIndex &>::of(&QCompleter::highlighted), this, &modifyTableDialog::onCompleterAccepted);

    // Connect add and remove buttons
    QObject::connect(ui->button_add, &QToolButton::clicked, this, &modifyTableDialog::onAddItem);
    QObject::connect(ui->button_remove, &QToolButton::clicked, this, &modifyTableDialog::onRemoveItem);
}

modifyTableDialog::~modifyTableDialog()
{
    this->parentWidget()->setEnabled(true);
    delete ui;
}

/// Refill the list from the service (id kept in Qt::UserRole)
void modifyTableDialog::reload()
{
    const QList<NamedItem> items = (list == Brands) ? products->brands() : products->categories();
    model->removeRows(0, model->rowCount());
    for (const NamedItem &item : items) {
        auto row = new QStandardItem(item.name);
        row->setData(item.id, Qt::UserRole);
        model->appendRow(row);
    }
}

void modifyTableDialog::onCompleterAccepted(const QModelIndex &index)
{
    auto proxy = qobject_cast<QAbstractProxyModel *>(searchCompleter->completionModel());
    const QModelIndex source = proxy ? proxy->mapToSource(index) : index;
    if (source.isValid())
        ui->tableView->selectRow(source.row());
}

void modifyTableDialog::onAddItem()
{
    const QString name = ui->lineEdit_add->text().trimmed();
    if (name.isEmpty())
        return;

    const qint64 id = (list == Brands) ? products->addBrand(name) : products->addCategory(name);
    if (!id) {
        QMessageBox::warning(this, tr("Error"), products->lastError());
        return;
    }
    ui->lineEdit_add->setText("");
    reload();
    emit listChanged();
}

void modifyTableDialog::onRemoveItem()
{
    if (!ui->tableView->selectionModel()->hasSelection())
        return;

    const QModelIndex indx = ui->tableView->currentIndex();
    const qint64 id = model->data(indx, Qt::UserRole).toLongLong();
    QString warningTitle = tr("ATENCION");
    QString warningBody = tr("¿Quitar \"%1\" de la lista?\n"
                             "Los articulos que ya la tienen la conservan.").arg(model->data(indx).toString());
    QMessageBox::StandardButton btn = QMessageBox::warning(this, warningTitle, warningBody, QMessageBox::Ok|QMessageBox::Cancel, QMessageBox::Cancel);
    if (btn != QMessageBox::Ok)
        return;

    const bool ok = (list == Brands) ? products->removeBrand(id) : products->removeCategory(id);
    if (!ok) {
        QMessageBox::warning(this, tr("Error"), products->lastError());
        return;
    }
    reload();
    emit listChanged();
}
