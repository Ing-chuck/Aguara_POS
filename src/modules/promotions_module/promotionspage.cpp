#include "promotionspage.h"
#include "promotiondialog.h"

#include <formcheck.h>
#include <livesearch.h>

#include <QCheckBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

enum Column { ColumnName, ColumnDiscount, ColumnFrom, ColumnTo, ColumnDays, ColumnState, ColumnCount };

QString dateText(const QDate &date)
{
    return date.toString(QStringLiteral("dd/MM/yyyy"));
}

QString stateOf(const Promotion &p, const QDate &today)
{
    if (!p.active)
        return QObject::tr("De baja");
    if (today < p.from)
        return QObject::tr("Programada");
    if (today > p.to)
        return QObject::tr("Terminada");
    return AguaraPromotion::runsOn(p, today) ? QObject::tr("Vigente") : QObject::tr("Hoy no aplica");
}

} // namespace

PromotionsPage::PromotionsPage(PromotionService *promotions, ProductService *products, InventoryService *inventory,
                               UserService *users, QWidget *parent)
    : AguaraWidget(parent), promotions(promotions), products(products), inventory(inventory), users(users)
{
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto header = new QWidget;
    AguaraStyle::setRole(header, AguaraStyle::PageHeader);
    header->setAttribute(Qt::WA_StyledBackground);
    auto headerRow = new QHBoxLayout(header);
    headerRow->setContentsMargins(24, 14, 24, 14);
    auto title = new QLabel(tr("Promociones"));
    AguaraStyle::setRole(title, AguaraStyle::Title);
    headerRow->addWidget(title, 1);
    auto addButton = new QPushButton(tr("Nueva promoción"));
    addButton->setObjectName(QStringLiteral("promoAdd"));
    AguaraStyle::setRole(addButton, AguaraStyle::Primary);
    headerRow->addWidget(addButton);
    layout->addWidget(header);

    auto body = new QVBoxLayout;
    body->setContentsMargins(24, 16, 24, 16);
    body->setSpacing(10);
    auto top = new QHBoxLayout;
    search = new QLineEdit;
    search->setObjectName(QStringLiteral("promoSearch"));
    search->setPlaceholderText(tr("Nombre de la promoción"));
    AguaraStyle::searchField(search);
    top->addWidget(search, 1);
    inactive = new QCheckBox(tr("Ver las dadas de baja"));
    inactive->setObjectName(QStringLiteral("promoInactive"));
    top->addWidget(inactive);
    body->addLayout(top);

    table = new QTableWidget(0, ColumnCount);
    table->setObjectName(QStringLiteral("promoTable"));
    table->setHorizontalHeaderLabels({ tr("Nombre"), tr("Descuento"), tr("Desde"), tr("Hasta"), tr("Días"), tr("Estado") });
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setAlternatingRowColors(true);
    table->setShowGrid(false);
    table->verticalHeader()->hide();
    table->horizontalHeader()->setHighlightSections(false);
    table->horizontalHeader()->setSectionResizeMode(ColumnName, QHeaderView::Stretch);
    table->setColumnWidth(ColumnDiscount, 150);
    table->setColumnWidth(ColumnFrom, 96);
    table->setColumnWidth(ColumnTo, 96);
    table->setColumnWidth(ColumnDays, 240);
    table->setColumnWidth(ColumnState, 110);
    body->addWidget(table, 1);

    problem = new QLabel;
    problem->setObjectName(QStringLiteral("promoProblem"));
    problem->setWordWrap(true);
    AguaraStyle::setRole(problem, AguaraStyle::Error);
    problem->hide();
    body->addWidget(problem);

    auto bottom = new QHBoxLayout;
    auto edit = new QPushButton(tr("Modificar"));
    edit->setObjectName(QStringLiteral("promoEdit"));
    bottom->addWidget(edit);
    auto copy = new QPushButton(tr("Duplicar"));
    copy->setObjectName(QStringLiteral("promoCopy"));
    copy->setToolTip(tr("Una promoción nueva con los mismos datos, para no armarla de cero"));
    bottom->addWidget(copy);
    auto show = new QPushButton(tr("Ver artículos"));
    show->setObjectName(QStringLiteral("promoShow"));
    bottom->addWidget(show);
    auto toggle = new QPushButton(tr("Dar de baja o reactivar"));
    toggle->setObjectName(QStringLiteral("promoToggle"));
    bottom->addWidget(toggle);
    bottom->addStretch();
    body->addLayout(bottom);
    layout->addLayout(body, 1);

    connect(search, &QLineEdit::textChanged, this, [this](const QString &text) { AguaraSearch::filterTable(table, text); });
    connect(inactive, &QCheckBox::toggled, this, &PromotionsPage::reload);
    connect(addButton, &QPushButton::clicked, this, &PromotionsPage::add);
    connect(edit, &QPushButton::clicked, this, &PromotionsPage::editSelected);
    connect(copy, &QPushButton::clicked, this, &PromotionsPage::copySelected);
    connect(show, &QPushButton::clicked, this, &PromotionsPage::showArticles);
    connect(toggle, &QPushButton::clicked, this, &PromotionsPage::toggleSelected);
    connect(table, &QTableWidget::cellDoubleClicked, this, &PromotionsPage::editSelected);
    reload();
}

void PromotionsPage::showEvent(QShowEvent *event)
{
    AguaraWidget::showEvent(event);
    reload();                           // a day can pass with the page open
    search->setFocus();
}

void PromotionsPage::reload()
{
    const qint64 keep = selected() ? selected()->id : 0;
    list = promotions->promotions(inactive->isChecked());
    const QDate today = QDate::currentDate();
    table->setRowCount(list.size());
    int current = list.isEmpty() ? -1 : 0;
    for (int row = 0; row < list.size(); ++row) {
        const Promotion &p = list.at(row);
        table->setItem(row, ColumnName, new QTableWidgetItem(p.name));
        table->setItem(row, ColumnDiscount, new QTableWidgetItem(AguaraPromotion::describe(p)));
        table->setItem(row, ColumnFrom, new QTableWidgetItem(dateText(p.from)));
        table->setItem(row, ColumnTo, new QTableWidgetItem(dateText(p.to)));
        table->setItem(row, ColumnDays, new QTableWidgetItem(AguaraPromotion::weekdayNames(p.weekdays)));
        table->setItem(row, ColumnState, new QTableWidgetItem(stateOf(p, today)));
        if (p.id == keep)
            current = row;
    }
    if (current >= 0)
        table->selectRow(current);
    AguaraSearch::filterTable(table, search->text());
}

const Promotion *PromotionsPage::selected() const
{
    const int row = table->currentRow();
    return row >= 0 && row < list.size() ? &list.at(row) : nullptr;
}

void PromotionsPage::add()
{
    if (!users->authorize(QStringLiteral("promociones.editar"), tr("Crear una promoción"), this))
        return;
    Promotion fresh;
    fresh.from = QDate::currentDate();
    fresh.to = fresh.from.addDays(6);
    if (PromotionDialog(promotions, products, inventory, fresh, this).exec() == QDialog::Accepted)
        reload();
}

void PromotionsPage::editSelected()
{
    if (!selected())
        return;
    const Promotion p = *selected();
    if (!users->authorize(QStringLiteral("promociones.editar"), tr("Modificar la promoción %1").arg(p.name), this))
        return;
    if (PromotionDialog(promotions, products, inventory, p, this).exec() == QDialog::Accepted)
        reload();
}

void PromotionsPage::copySelected()
{
    if (!selected())
        return;
    Promotion copy = *selected();
    if (!users->authorize(QStringLiteral("promociones.editar"), tr("Duplicar la promoción %1").arg(copy.name), this))
        return;
    copy.id = 0;
    copy.active = true;
    copy.name = tr("%1 (copia)").arg(copy.name);
    if (PromotionDialog(promotions, products, inventory, copy, this).exec() == QDialog::Accepted)
        reload();
}

void PromotionsPage::showArticles()
{
    if (selected())
        PromotionArticlesDialog(promotions, *selected(), this).exec();
}

void PromotionsPage::toggleSelected()
{
    if (!selected())
        return;
    const Promotion p = *selected();
    if (p.active && !AguaraForm::confirmRemoval(this, tr("Dar de baja"),
            tr("%1 deja de aplicarse en las ventas. Las ventas hechas no cambian, y se puede reactivar.").arg(p.name),
            tr("Dar de baja")))
        return;
    if (!users->authorize(QStringLiteral("promociones.editar"),
                          (p.active ? tr("Dar de baja la promoción %1") : tr("Reactivar la promoción %1")).arg(p.name), this))
        return;
    const bool done = promotions->setPromotionActive(p.id, !p.active);
    problem->setVisible(!done);
    problem->setText(promotions->lastError());
    reload();
}
