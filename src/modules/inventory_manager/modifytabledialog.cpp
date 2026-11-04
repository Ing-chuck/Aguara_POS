#include "modifytabledialog.h"
#include "stockdialogs.h"
#include "ui_modifytabledialog.h"

#include <aguarastyle.h>

#include <QAbstractProxyModel>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
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
    setAttribute(Qt::WA_DeleteOnClose);
    ui->setupUi(this);
    setWindowModality(Qt::WindowModal);     // over the article dialog

    AguaraStyle::searchField(ui->lineEdit_search);
    ui->button_add->setIcon(AguaraStyle::icon(QStringLiteral("plus")));
    ui->button_remove->setIcon(AguaraStyle::icon(QStringLiteral("trash"), AguaraStyle::color(AguaraStyle::Color::Danger)));
    ui->buttonBox->button(QDialogButtonBox::Close)->setText(tr("Cerrar"));
    connect(ui->lineEdit_add, &QLineEdit::returnPressed, this, &modifyTableDialog::onAddItem);
    keepEnterInDialog(this);

    // Make line edits write in all caps
    QObject::connect(ui->lineEdit_search, &QLineEdit::textEdited, this, [this](const QString &text){ui->lineEdit_search->setText(text.toUpper());});
    QObject::connect(ui->lineEdit_add, &QLineEdit::textEdited, this, [this](const QString &text){ui->lineEdit_add->setText(text.toUpper());});

    if (list == Brands) {
        ui->label_title->setText(tr("Marcas"));
        ui->label_subtitle->setText(tr("Los artículos que ya tienen una marca la conservan aunque se quite de la lista."));
        setWindowTitle(tr("Marcas"));
        model->setHorizontalHeaderLabels({ tr("Marcas") });
    }
    else {
        ui->label_title->setText(tr("Rubros"));
        ui->label_subtitle->setText(tr("Los artículos que ya tienen un rubro lo conservan aunque se quite de la lista."));
        setWindowTitle(tr("Rubros"));
        model->setHorizontalHeaderLabels({ tr("Rubros") });
    }
    reload();

    ui->tableView->setModel(model);
    ui->tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->tableView->verticalHeader()->setDefaultSectionSize(32);

    // Search completer
    searchCompleter = new QCompleter(model, this);
    searchCompleter->setCaseSensitivity(Qt::CaseInsensitive);
    searchCompleter->setCompletionMode(QCompleter::InlineCompletion);
    ui->lineEdit_search->setCompleter(searchCompleter);

    connect(searchCompleter, QOverload<const QModelIndex &>::of(&QCompleter::highlighted), this, &modifyTableDialog::onCompleterAccepted);

    // Connect add and remove buttons
    QObject::connect(ui->button_add, &QAbstractButton::clicked, this, &modifyTableDialog::onAddItem);
    QObject::connect(ui->button_remove, &QAbstractButton::clicked, this, &modifyTableDialog::onRemoveItem);

    if (list == Categories)
        buildParentChoice();
}

modifyTableDialog::~modifyTableDialog()
{
    delete ui;
}

/// Refill the list from the service (id kept in Qt::UserRole)
void modifyTableDialog::reload()
{
    const QList<NamedItem> items = (list == Brands) ? products->brands() : products->categories();
    model->removeRows(0, model->rowCount());
    for (const NamedItem &item : items) {
        auto row = new QStandardItem(item.path.isEmpty() ? item.name : item.path);
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

    const qint64 id = (list == Brands) ? products->addBrand(name)
                                       : products->addCategory(name, parentCombo ? parentCombo->currentData().toLongLong() : 0);
    if (!id) {
        QMessageBox::warning(this, tr("Error"), products->lastError());
        return;
    }
    ui->lineEdit_add->setText("");
    reload();
    reloadParents();
    emit listChanged();
}

/// Categories nest: where a new one goes, and where the selected one is.
void modifyTableDialog::buildParentChoice()
{
    parentCombo = new QComboBox;
    parentCombo->setObjectName(QStringLiteral("parentCombo"));
    moveButton = new QPushButton(tr("Mover aquí"));
    moveButton->setAutoDefault(false);
    moveButton->setToolTip(tr("Pone el rubro seleccionado dentro del elegido"));
    auto row = new QHBoxLayout;
    row->setSpacing(8);
    auto label = new QLabel(tr("Dentro de:"));
    row->addWidget(label);
    row->addWidget(parentCombo, 1);
    row->addWidget(moveButton);
    ui->dialogLayout->insertLayout(ui->dialogLayout->indexOf(ui->tableView) + 1, row);
    reloadParents();

    connect(moveButton, &QPushButton::clicked, this, [this]() {
        if (!ui->tableView->selectionModel()->hasSelection())
            return;
        const qint64 id = model->data(ui->tableView->currentIndex(), Qt::UserRole).toLongLong();
        if (!products->setCategoryParent(id, parentCombo->currentData().toLongLong())) {
            QMessageBox::warning(this, tr("No se pudo mover"), products->lastError());
            return;
        }
        reload();
        reloadParents();
        emit listChanged();
    });
}

void modifyTableDialog::reloadParents()
{
    if (!parentCombo)
        return;
    const QVariant current = parentCombo->currentData();
    parentCombo->clear();
    parentCombo->addItem(tr("(ninguno)"), qint64(0));
    for (const NamedItem &c : products->categories())
        parentCombo->addItem(c.path, c.id);
    parentCombo->setCurrentIndex(qMax(parentCombo->findData(current), 0));
}

void modifyTableDialog::onRemoveItem()
{
    if (!ui->tableView->selectionModel()->hasSelection())
        return;

    const QModelIndex indx = ui->tableView->currentIndex();
    const qint64 id = model->data(indx, Qt::UserRole).toLongLong();
    QString warningTitle = tr("Quitar de la lista");
    QString warningBody = tr("¿Quitar \"%1\" de la lista?\n"
                             "Los artículos que ya la tienen la conservan.").arg(model->data(indx).toString());
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
