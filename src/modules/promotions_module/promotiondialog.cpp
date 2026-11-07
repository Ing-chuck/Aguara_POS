#include "promotiondialog.h"

#include <formcheck.h>
#include <livesearch.h>

#include <QAbstractItemModel>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableView>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace {

enum RuleColumn { RuleWhat, RuleName, RuleMode };

constexpr int MostArticlesShown = 2000;

QVBoxLayout *dialogHeader(QDialog *dialog, const QString &title, const QString &subtitle)
{
    auto layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(24, 20, 24, 18);
    layout->setSpacing(10);
    dialog->setWindowTitle(title);
    auto heading = new QLabel(title);
    AguaraStyle::setRole(heading, AguaraStyle::Title);
    layout->addWidget(heading);
    if (!subtitle.isEmpty()) {
        auto sub = new QLabel(subtitle);
        sub->setWordWrap(true);
        AguaraStyle::setRole(sub, AguaraStyle::Subtitle);
        layout->addWidget(sub);
    }
    return layout;
}

QLabel *section(const QString &text)
{
    auto label = new QLabel(text);
    AguaraStyle::setRole(label, AguaraStyle::Section);
    return label;
}

QTableWidget *makeTable(const QStringList &headers)
{
    auto table = new QTableWidget(0, headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setAlternatingRowColors(true);
    table->setShowGrid(false);
    table->verticalHeader()->hide();
    table->horizontalHeader()->setHighlightSections(false);
    return table;
}

QString targetName(PromotionRule::Target target)
{
    switch (target) {
    case PromotionRule::Article:  return QObject::tr("Artículo");
    case PromotionRule::Category: return QObject::tr("Rubro");
    case PromotionRule::Brand:    return QObject::tr("Marca");
    }
    return QString();
}

bool sameTarget(const PromotionRule &a, const PromotionRule &b)
{
    return a.target == b.target && a.code == b.code && a.id == b.id;
}

QDateEdit *dateField(const QDate &date, const char *objectName)
{
    auto edit = new QDateEdit(date);
    edit->setObjectName(QLatin1String(objectName));
    edit->setCalendarPopup(true);
    edit->setDisplayFormat(QStringLiteral("dd/MM/yyyy"));
    return edit;
}

} // namespace

void EnterKeepingDialog::keyPressEvent(QKeyEvent *event)
{
    if (event->key() != Qt::Key_Return && event->key() != Qt::Key_Enter)
        QDialog::keyPressEvent(event);
}

PromotionDialog::PromotionDialog(PromotionService *promotions, ProductService *products, InventoryService *inventory,
                                 const Promotion &promotion, QWidget *parent)
    : EnterKeepingDialog(parent), promotions(promotions), products(products), inventory(inventory)
    , id(promotion.id), active(promotion.active), rules(promotion.rules)
{
    auto layout = dialogHeader(this, promotion.id == 0 ? tr("Nueva promoción") : tr("Modificar la promoción"), QString());
    resize(760, 700);

    auto columns = new QHBoxLayout;
    columns->setSpacing(24);
    columns->addWidget(buildDiscount(), 1);
    columns->addWidget(buildDates(), 1);
    layout->addLayout(columns);
    layout->addWidget(buildSelection(), 1);

    problem = AguaraForm::errorLabel(QStringLiteral("promoError"));
    layout->addWidget(problem);
    layout->addWidget(AguaraForm::requiredNote());

    auto buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setText(tr("Guardar"));
    buttons->button(QDialogButtonBox::Save)->setObjectName(QStringLiteral("promoSave"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancelar"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    // the promotion as it came
    name->setText(promotion.name);
    kind->setCurrentIndex(kind->findData(int(promotion.kind)));
    if (promotion.kind == PromotionKind::Percent && promotion.value > 0)
        value->setText(QString::number(promotion.value));
    else if (promotion.value > 0)
        value->setText(AguaraMoney::format(promotion.value));
    buy->setValue(qMax(promotion.buy, 2));
    pay->setValue(qMax(promotion.pay, 1));
    if (promotion.minimum > 0)
        minimum->setText(AguaraQuantity::format(promotion.minimum));
    for (int day = 0; day < days.size(); ++day)
        days.at(day)->setChecked(promotion.weekdays & (1 << day));
    from->setDate(promotion.from.isValid() ? promotion.from : QDate::currentDate());
    to->setDate(promotion.to.isValid() ? promotion.to : QDate::currentDate());
    (promotion.everything ? everything : chosen)->setChecked(true);
    inverted->setChecked(promotion.inverted);
    categoryAndBrand->setChecked(promotion.categoryAndBrand);

    kindChanged();
    renderRules();
    selectionChanged();
    connect(kind, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PromotionDialog::kindChanged);
    connect(everything, &QRadioButton::toggled, this, &PromotionDialog::selectionChanged);
    connect(inverted, &QCheckBox::toggled, this, &PromotionDialog::selectionChanged);
    connect(categoryAndBrand, &QCheckBox::toggled, this, &PromotionDialog::selectionChanged);
    name->setFocus();
}

QWidget *PromotionDialog::buildDiscount()
{
    auto box = new QWidget;
    auto form = new QFormLayout(box);
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(8);
    form->addRow(section(tr("DESCUENTO")));

    name = new QLineEdit;
    name->setObjectName(QStringLiteral("promoName"));
    name->setPlaceholderText(tr("Por ejemplo Semana del lácteo"));
    form->addRow(AguaraForm::required(tr("Nombre:")), name);

    kind = new QComboBox;
    kind->setObjectName(QStringLiteral("promoKind"));
    kind->addItem(tr("Porcentaje"), int(PromotionKind::Percent));
    kind->addItem(tr("Importe menos por unidad"), int(PromotionKind::AmountOff));
    kind->addItem(tr("Precio de promoción"), int(PromotionKind::FixedPrice));
    kind->addItem(tr("Lleva y paga (3x2)"), int(PromotionKind::BuyPay));
    form->addRow(tr("Tipo:"), kind);

    valueLabel = new QLabel;
    value = new QLineEdit;
    value->setObjectName(QStringLiteral("promoValue"));
    form->addRow(valueLabel, value);

    buyLabel = new QLabel(tr("Lleva:"));
    buy = new QSpinBox;
    buy->setObjectName(QStringLiteral("promoBuy"));
    buy->setRange(2, 999);
    form->addRow(buyLabel, buy);
    payLabel = new QLabel(tr("Paga:"));
    pay = new QSpinBox;
    pay->setObjectName(QStringLiteral("promoPay"));
    pay->setRange(1, 998);
    form->addRow(payLabel, pay);

    minimumLabel = new QLabel(tr("Cantidad mínima:"));
    minimum = new QLineEdit;
    minimum->setObjectName(QStringLiteral("promoMinimum"));
    minimum->setPlaceholderText(tr("Desde la primera unidad"));
    minimum->setToolTip(tr("Lo que debe llevar en la línea para tener el descuento"));
    form->addRow(minimumLabel, minimum);
    return box;
}

QWidget *PromotionDialog::buildDates()
{
    auto box = new QWidget;
    auto form = new QFormLayout(box);
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(8);
    form->addRow(section(tr("FECHAS")));

    from = dateField(QDate::currentDate(), "promoFrom");
    to = dateField(QDate::currentDate(), "promoTo");
    AguaraForm::keepInOrder(from, to);
    form->addRow(tr("Desde:"), from);
    form->addRow(tr("Hasta:"), to);

    auto week = new QGridLayout;
    week->setSpacing(4);
    const QStringList names = { tr("Lun"), tr("Mar"), tr("Mié"), tr("Jue"), tr("Vie"), tr("Sáb"), tr("Dom") };
    for (int day = 0; day < names.size(); ++day) {
        auto check = new QCheckBox(names.at(day));
        check->setObjectName(QStringLiteral("promoDay%1").arg(day));
        days << check;
        week->addWidget(check, day / 4, day % 4);
    }
    form->addRow(tr("Días:"), week);
    return box;
}

QWidget *PromotionDialog::buildSelection()
{
    auto box = new QWidget;
    auto layout = new QVBoxLayout(box);
    layout->setContentsMargins(0, 6, 0, 0);
    layout->setSpacing(8);
    layout->addWidget(section(tr("ARTÍCULOS")));

    auto mode = new QHBoxLayout;
    everything = new QRadioButton(tr("Todos los artículos"));
    everything->setObjectName(QStringLiteral("promoEverything"));
    chosen = new QRadioButton(tr("Los elegidos abajo"));
    chosen->setObjectName(QStringLiteral("promoChosen"));
    auto group = new QButtonGroup(this);
    group->addButton(everything);
    group->addButton(chosen);
    inverted = new QCheckBox(tr("Invertir: todos menos los elegidos"));
    inverted->setObjectName(QStringLiteral("promoInverted"));
    mode->addWidget(everything);
    mode->addWidget(chosen);
    mode->addWidget(inverted);
    mode->addStretch();
    layout->addLayout(mode);

    categoryAndBrand = new QCheckBox(tr("Rubro y marca a la vez (los de ese rubro que además sean de esa marca)"));
    categoryAndBrand->setObjectName(QStringLiteral("promoBoth"));
    layout->addWidget(categoryAndBrand);

    auto addRow = new QHBoxLayout;
    addRow->setSpacing(8);
    target = new QComboBox;
    target->setObjectName(QStringLiteral("promoTarget"));
    target->addItem(targetName(PromotionRule::Article));
    target->addItem(targetName(PromotionRule::Category));
    target->addItem(targetName(PromotionRule::Brand));
    addRow->addWidget(target);

    targetInput = new QStackedWidget;
    article = new QLineEdit;
    article->setObjectName(QStringLiteral("promoArticle"));
    article->setPlaceholderText(tr("Escanee, o escriba el código o la descripción"));
    targetInput->addWidget(article);
    category = new QComboBox;
    category->setObjectName(QStringLiteral("promoCategory"));
    for (const NamedItem &item : products->categories())
        category->addItem(item.path.isEmpty() ? item.name : item.path, item.id);
    targetInput->addWidget(category);
    brand = new QComboBox;
    brand->setObjectName(QStringLiteral("promoBrand"));
    for (const NamedItem &item : products->brands())
        brand->addItem(item.name, item.id);
    targetInput->addWidget(brand);
    targetInput->setFixedHeight(article->sizeHint().height() + 2);
    addRow->addWidget(targetInput, 1);

    includeButton = new QPushButton(AguaraStyle::icon(QStringLiteral("plus")), tr("Incluir"));
    includeButton->setObjectName(QStringLiteral("promoInclude"));
    includeButton->setAutoDefault(false);
    addRow->addWidget(includeButton);
    auto excludeButton = new QPushButton(tr("Excluir"));
    excludeButton->setObjectName(QStringLiteral("promoExclude"));
    excludeButton->setAutoDefault(false);
    excludeButton->setToolTip(tr("Lo excluido queda fuera aunque otra regla lo elija"));
    addRow->addWidget(excludeButton);
    auto listButton = new QPushButton(AguaraStyle::icon(QStringLiteral("list")), tr("Elegir varios"));
    listButton->setObjectName(QStringLiteral("promoPick"));
    listButton->setAutoDefault(false);
    addRow->addWidget(listButton);
    layout->addLayout(addRow);

    ruleTable = makeTable({ tr("Tipo"), tr("Nombre"), tr("Regla") });
    ruleTable->setObjectName(QStringLiteral("promoRules"));
    ruleTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    ruleTable->horizontalHeader()->setSectionResizeMode(RuleName, QHeaderView::Stretch);
    ruleTable->setColumnWidth(RuleWhat, 90);
    ruleTable->setColumnWidth(RuleMode, 90);
    layout->addWidget(ruleTable, 1);

    auto bottom = new QHBoxLayout;
    auto removeButton = new QPushButton(AguaraStyle::icon(QStringLiteral("trash"), AguaraStyle::color(AguaraStyle::Color::Danger)),
                                        tr("Quitar de la lista"));
    removeButton->setObjectName(QStringLiteral("promoRuleRemove"));
    removeButton->setAutoDefault(false);
    bottom->addWidget(removeButton);
    bottom->addStretch();
    reach = new QLabel;
    reach->setObjectName(QStringLiteral("promoReach"));
    AguaraStyle::setRole(reach, AguaraStyle::Muted);
    bottom->addWidget(reach);
    auto showButton = new QPushButton(tr("Ver artículos"));
    showButton->setObjectName(QStringLiteral("promoPreview"));
    showButton->setAutoDefault(false);
    bottom->addWidget(showButton);
    layout->addLayout(bottom);

    auto suggestions = new SuggestionBox(article, SuggestionBox::articles(products, this));
    connect(suggestions, &SuggestionBox::chosen, this, [this](const QString &code) {
        article->setText(code);
        addArticle(false);
    });
    connect(article, &QLineEdit::returnPressed, this, [this]() { addArticle(false); });
    connect(target, QOverload<int>::of(&QComboBox::currentIndexChanged), targetInput, &QStackedWidget::setCurrentIndex);
    connect(includeButton, &QPushButton::clicked, this, [this]() { addChosen(false); });
    connect(excludeButton, &QPushButton::clicked, this, [this]() { addChosen(true); });
    connect(listButton, &QPushButton::clicked, this, &PromotionDialog::addFromList);
    connect(removeButton, &QPushButton::clicked, this, &PromotionDialog::removeRule);
    connect(showButton, &QPushButton::clicked, this, [this]() {
        PromotionArticlesDialog(promotions, collect(), this).exec();
    });
    return box;
}

void PromotionDialog::addRule(const PromotionRule &rule)
{
    // once on the list: choosing it again changes its side
    for (PromotionRule &known : rules) {
        if (sameTarget(known, rule)) {
            known.excluded = rule.excluded;
            return;
        }
    }
    rules << rule;
}

void PromotionDialog::addChosen(bool excluded)
{
    if (target->currentIndex() == PromotionRule::Article) {
        addArticle(excluded);
        return;
    }
    QComboBox *combo = target->currentIndex() == PromotionRule::Category ? category : brand;
    if (combo->currentIndex() < 0)
        return;
    PromotionRule rule;
    rule.target = PromotionRule::Target(target->currentIndex());
    rule.id = combo->currentData().toLongLong();
    rule.name = combo->currentText();
    rule.excluded = excluded;
    addRule(rule);
    renderRules();
    selectionChanged();
}

void PromotionDialog::addArticle(bool excluded)
{
    const QString text = article->text().trimmed();
    if (text.isEmpty())
        return;
    const ScanResult scan = inventory->resolveScan(text);
    if (!scan.found) {
        AguaraForm::showError(problem, tr("No hay ningún artículo con el código %1.").arg(text), article);
        return;
    }
    problem->hide();
    PromotionRule rule;
    rule.target = PromotionRule::Article;
    rule.code = scan.product.code;
    rule.name = scan.product.description;
    rule.excluded = excluded;
    addRule(rule);
    article->clear();
    article->setFocus();
    renderRules();
    selectionChanged();
}

void PromotionDialog::addFromList()
{
    PromotionPickDialog pick(products, this);
    if (pick.exec() != QDialog::Accepted)
        return;
    for (const PromotionRule &rule : pick.rules())
        addRule(rule);
    renderRules();
    selectionChanged();
}

void PromotionDialog::removeRule()
{
    QList<int> rows;
    for (const QModelIndex &index : ruleTable->selectionModel()->selectedRows())
        rows << index.row();
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    for (int row : qAsConst(rows))
        rules.removeAt(row);
    renderRules();
    selectionChanged();
}

void PromotionDialog::renderRules()
{
    ruleTable->setRowCount(rules.size());
    for (int row = 0; row < rules.size(); ++row) {
        const PromotionRule &rule = rules.at(row);
        ruleTable->setItem(row, RuleWhat, new QTableWidgetItem(targetName(rule.target)));
        ruleTable->setItem(row, RuleName, new QTableWidgetItem(rule.name));
        ruleTable->setItem(row, RuleMode, new QTableWidgetItem(rule.excluded ? tr("Excluye") : tr("Incluye")));
    }
}

void PromotionDialog::kindChanged()
{
    const auto current = PromotionKind(kind->currentData().toInt());
    const bool buyPay = current == PromotionKind::BuyPay;
    valueLabel->setText(AguaraForm::required(current == PromotionKind::Percent ? tr("Porcentaje:")
                        : current == PromotionKind::AmountOff ? tr("Importe menos:") : tr("Precio:")));
    valueLabel->setVisible(!buyPay);
    value->setVisible(!buyPay);
    buyLabel->setVisible(buyPay);
    buy->setVisible(buyPay);
    payLabel->setVisible(buyPay);
    pay->setVisible(buyPay);
    minimumLabel->setVisible(!buyPay);       // it is in the "lleva" already
    minimum->setVisible(!buyPay);
}

void PromotionDialog::selectionChanged()
{
    const bool all = everything->isChecked();
    inverted->setEnabled(!all);
    categoryAndBrand->setEnabled(!all);
    includeButton->setEnabled(!all);        // with everything in, only exclusions make sense
    if (all)
        inverted->setChecked(false);

    const Promotion p = collect();
    bool picksSomething = p.everything;
    for (const PromotionRule &rule : p.rules)
        picksSomething = picksSomething || !rule.excluded;
    if (!picksSomething) {
        reach->setText(tr("Sin artículos elegidos"));
        return;
    }
    const int count = promotions->articlesIn(p).size();
    reach->setText(count == 1 ? tr("Alcanza a 1 artículo") : tr("Alcanza a %1 artículos").arg(count));
}

Promotion PromotionDialog::collect() const
{
    Promotion p;
    p.id = id;
    p.active = active;
    p.name = name->text();
    p.kind = PromotionKind(kind->currentData().toInt());
    if (p.kind == PromotionKind::Percent) {
        p.value = value->text().trimmed().toLongLong();
    } else if (p.kind != PromotionKind::BuyPay) {
        AguaraMoney::parse(value->text(), &p.value);
    } else {
        p.buy = buy->value();
        p.pay = pay->value();
    }
    if (p.kind != PromotionKind::BuyPay)
        AguaraQuantity::parse(minimum->text(), &p.minimum);
    p.from = from->date();
    p.to = to->date();
    p.weekdays = 0;
    for (int day = 0; day < days.size(); ++day) {
        if (days.at(day)->isChecked())
            p.weekdays |= 1 << day;
    }
    p.everything = everything->isChecked();
    p.inverted = !p.everything && inverted->isChecked();
    p.categoryAndBrand = !p.everything && categoryAndBrand->isChecked();
    for (const PromotionRule &rule : rules) {
        if (rule.excluded || !p.everything)     // with everything in, the picks say nothing
            p.rules << rule;
    }
    return p;
}

void PromotionDialog::accept()
{
    if (!minimum->text().trimmed().isEmpty() && minimum->isVisibleTo(this)) {
        Quantity read = 0;
        if (!AguaraQuantity::parse(minimum->text(), &read) || read < 0) {
            AguaraForm::showError(problem, tr("La cantidad mínima no es válida."), minimum);
            return;
        }
    }
    if (promotions->savePromotion(collect()) == 0) {
        AguaraForm::showError(problem, promotions->lastError());
        return;
    }
    EnterKeepingDialog::accept();
}

// --- PromotionPickDialog -----------------------------------------------------

PromotionPickDialog::PromotionPickDialog(ProductService *products, QWidget *parent) : EnterKeepingDialog(parent)
{
    auto layout = dialogHeader(this, tr("Elegir artículos"),
        tr("Marque varios con Ctrl o Mayús, y use el buscador para ir de una búsqueda a otra."));
    resize(720, 560);

    auto search = new QLineEdit;
    search->setObjectName(QStringLiteral("pickSearch"));
    search->setPlaceholderText(tr("Código, descripción o marca"));
    AguaraStyle::searchField(search);
    layout->addWidget(search);

    proxy = new SearchProxyModel({ int(ProductColumn::Code), int(ProductColumn::Description), int(ProductColumn::Brand) }, this);
    proxy->setSourceModel(products->createProductTableModel(this));
    view = new QTableView;
    view->setObjectName(QStringLiteral("pickList"));
    view->setModel(proxy);
    view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view->setSelectionBehavior(QAbstractItemView::SelectRows);
    view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    view->setAlternatingRowColors(true);
    view->setShowGrid(false);
    view->verticalHeader()->hide();
    view->horizontalHeader()->setHighlightSections(false);
    const QList<ProductColumn> shown = { ProductColumn::Code, ProductColumn::Description, ProductColumn::Brand,
                                         ProductColumn::Category, ProductColumn::Price };
    for (int column = 0; column < int(ProductColumn::Count); ++column)
        view->setColumnHidden(column, !shown.contains(ProductColumn(column)));
    view->horizontalHeader()->setSectionResizeMode(int(ProductColumn::Description), QHeaderView::Stretch);
    view->setColumnWidth(int(ProductColumn::Brand), 130);
    layout->addWidget(view, 1);

    auto buttons = new QDialogButtonBox;
    auto include = buttons->addButton(tr("Incluir marcados"), QDialogButtonBox::AcceptRole);
    include->setObjectName(QStringLiteral("pickInclude"));
    include->setMinimumWidth(170);          // the theme's bold doesn't fit otherwise
    auto exclude = buttons->addButton(tr("Excluir marcados"), QDialogButtonBox::ActionRole);
    exclude->setObjectName(QStringLiteral("pickExclude"));
    exclude->setAutoDefault(false);
    buttons->addButton(QDialogButtonBox::Cancel)->setText(tr("Cancelar"));
    layout->addWidget(buttons);

    auto apply = [this, search]() { proxy->setSearchText(search->text()); };
    AguaraSearch::afterTyping(search, apply);
    connect(include, &QPushButton::clicked, this, [this]() { finish(false); });
    connect(exclude, &QPushButton::clicked, this, [this]() { finish(true); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    search->setFocus();
}

void PromotionPickDialog::finish(bool exclude)
{
    picked.clear();
    for (const QModelIndex &row : view->selectionModel()->selectedRows()) {
        PromotionRule rule;
        rule.target = PromotionRule::Article;
        rule.code = row.siblingAtColumn(int(ProductColumn::Code)).data().toString();
        rule.name = row.siblingAtColumn(int(ProductColumn::Description)).data().toString();
        rule.excluded = exclude;
        picked << rule;
    }
    accept();
}

// --- PromotionArticlesDialog -------------------------------------------------

PromotionArticlesDialog::PromotionArticlesDialog(PromotionService *promotions, const Promotion &promotion, QWidget *parent)
    : QDialog(parent)
{
    const QList<Product> articles = promotions->articlesIn(promotion);
    auto layout = dialogHeader(this, tr("Artículos de la promoción"),
        promotion.name.isEmpty() ? QString() : QStringLiteral("%1 · %2").arg(promotion.name, AguaraPromotion::describe(promotion)));
    resize(760, 560);

    auto search = new QLineEdit;
    search->setObjectName(QStringLiteral("promoArticleSearch"));
    search->setPlaceholderText(tr("Código o descripción"));
    AguaraStyle::searchField(search);
    layout->addWidget(search);

    const bool perUnit = promotion.kind != PromotionKind::BuyPay;
    auto table = makeTable({ tr("Código"), tr("Descripción"), tr("Marca"), tr("Rubro"), tr("Precio"), tr("Con promoción") });
    table->setObjectName(QStringLiteral("promoArticles"));
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->setColumnWidth(2, 130);
    table->setColumnWidth(5, 130);
    table->setColumnHidden(5, !perUnit);        // a 3x2 has no price of its own
    const int rows = qMin(articles.size(), MostArticlesShown);
    table->setRowCount(rows);
    for (int row = 0; row < rows; ++row) {
        const Product &p = articles.at(row);
        table->setItem(row, 0, new QTableWidgetItem(p.code));
        table->setItem(row, 1, new QTableWidgetItem(p.description));
        table->setItem(row, 2, new QTableWidgetItem(p.brandName));
        table->setItem(row, 3, new QTableWidgetItem(p.categoryName));
        const auto money = [](Money amount) {
            auto item = new QTableWidgetItem(AguaraMoney::format(amount));
            item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            return item;
        };
        table->setItem(row, 4, money(p.price));
        if (perUnit) {
            SaleLine one;
            one.unitPrice = p.price;
            Promotion anyQuantity = promotion;
            anyQuantity.minimum = 0;            // the price, whatever it takes to get it
            table->setItem(row, 5, money(p.price - AguaraPromotion::discount(anyQuantity, one)));
        }
    }
    layout->addWidget(table, 1);

    auto note = new QLabel(articles.size() > rows
        ? tr("Se muestran los primeros %1 de %2 artículos.").arg(rows).arg(articles.size())
        : tr("%1 artículos.").arg(articles.size()));
    note->setObjectName(QStringLiteral("promoArticlesNote"));
    AguaraStyle::setRole(note, AguaraStyle::Muted);
    layout->addWidget(note);

    auto buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->button(QDialogButtonBox::Close)->setText(tr("Cerrar"));
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    connect(search, &QLineEdit::textChanged, this, [table](const QString &text) { AguaraSearch::filterTable(table, text); });
}
