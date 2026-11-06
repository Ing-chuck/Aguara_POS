#include "modifytabledialog.h"
#include "stockdialogs.h"
#include "ui_modifytabledialog.h"

#include <aguarastyle.h>
#include <formcheck.h>
#include <livesearch.h>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
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

    visible = new SearchProxyModel({ 0 }, this);
    visible->setSourceModel(model);
    ui->tableView->setModel(visible);
    ui->tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->tableView->verticalHeader()->setDefaultSectionSize(32);

    // The list narrows down as you type
    connect(ui->lineEdit_search, &QLineEdit::textChanged, visible, &SearchProxyModel::setSearchText);

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

void modifyTableDialog::onAddItem()
{
    const QString name = ui->lineEdit_add->text().trimmed();
    if (name.isEmpty()) {
        ui->lineEdit_add->setFocus();
        return;
    }

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
        const qint64 id = ui->tableView->currentIndex().data(Qt::UserRole).toLongLong();
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
    const qint64 id = indx.data(Qt::UserRole).toLongLong();
    if (!AguaraForm::confirmRemoval(this, tr("Quitar de la lista"),
            tr("\"%1\" deja de aparecer en la lista. Los artículos que ya la tienen la conservan.")
                .arg(indx.data().toString()), tr("Quitar")))
        return;

    const bool ok = (list == Brands) ? products->removeBrand(id) : products->removeCategory(id);
    if (!ok) {
        QMessageBox::warning(this, tr("Error"), products->lastError());
        return;
    }
    reload();
    reloadParents();
    emit listChanged();
}
