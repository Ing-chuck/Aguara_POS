#include "salesmodule.h"
#include "cashdialogs.h"
#include "saledialogs.h"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

enum LineColumn { ColumnCode, ColumnDescription, ColumnQuantity, ColumnPrice, ColumnDiscount, ColumnAmount };

QLabel *figure(QGridLayout *grid, int row, const QString &name)
{
    auto label = new QLabel(name);
    AguaraStyle::setRole(label, AguaraStyle::Muted);
    auto value = new QLabel;
    value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    grid->addWidget(label, row, 0);
    grid->addWidget(value, row, 1);
    return value;
}

} // namespace

SalesModule::SalesModule(ProductService *products, InventoryService *inventory, UserService *users,
                         CashService *cash, SalesService *sales, QWidget *parent)
    : AguaraWidget(parent), products(products), inventory(inventory), users(users), cash(cash), sales(sales)
{
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto header = new QWidget;
    AguaraStyle::setRole(header, AguaraStyle::PageHeader);
    header->setAttribute(Qt::WA_StyledBackground);
    auto headerRow = new QHBoxLayout(header);
    headerRow->setContentsMargins(24, 14, 24, 14);
    auto titles = new QVBoxLayout;
    titles->setSpacing(2);
    auto title = new QLabel(tr("Ventas"));
    AguaraStyle::setRole(title, AguaraStyle::Title);
    subtitle = new QLabel;
    subtitle->setObjectName(QStringLiteral("saleSubtitle"));
    AguaraStyle::setRole(subtitle, AguaraStyle::Subtitle);
    titles->addWidget(title);
    titles->addWidget(subtitle);
    headerRow->addLayout(titles, 1);
    cashButton = new QPushButton(tr("Caja"));
    cashButton->setObjectName(QStringLiteral("cashMenuButton"));
    cashButton->setFocusPolicy(Qt::NoFocus);
    cashButton->setMinimumWidth(96);
    buildCashMenu(cashButton);
    headerRow->addWidget(cashButton);
    layout->addWidget(header);

    pages = new QStackedWidget;
    pages->addWidget(buildClosedPage());
    pages->addWidget(buildSalePage());
    layout->addWidget(pages, 1);

    renderSale();
    refreshSession();
}

void SalesModule::buildCashMenu(QPushButton *button)
{
    auto menu = new QMenu(button);
    const auto add = [&](const QString &text, const char *name, void (SalesModule::*action)()) {
        QAction *item = menu->addAction(text);
        item->setObjectName(QLatin1String(name));
        connect(item, &QAction::triggered, this, action);
    };
    add(tr("Ventas de la sesión"), "actionSessionSales", &SalesModule::showSessionSales);
    add(tr("Movimiento de caja"), "actionCashMovement", &SalesModule::cashMovement);
    add(tr("Cerrar caja"), "actionCloseRegister", &SalesModule::closeRegister);
    menu->addSeparator();
    add(tr("Planillas anteriores"), "actionSessions", &SalesModule::showSessions);
    add(tr("Nombre de esta caja"), "actionRenameRegister", &SalesModule::renameRegister);
    button->setMenu(menu);
}

QWidget *SalesModule::buildClosedPage()
{
    auto page = new QWidget;
    auto layout = new QVBoxLayout(page);
    layout->setSpacing(8);
    layout->addStretch(2);
    auto icon = new QLabel;
    icon->setPixmap(AguaraStyle::icon(QStringLiteral("lock"),
        AguaraStyle::color(AguaraStyle::Color::BorderStrong)).pixmap(56, 56));
    layout->addWidget(icon, 0, Qt::AlignHCenter);
    closedTitle = new QLabel;
    closedTitle->setObjectName(QStringLiteral("closedTitle"));
    closedTitle->setStyleSheet(QStringLiteral("font-size: 12pt; font-weight: 600;"));
    layout->addWidget(closedTitle, 0, Qt::AlignHCenter);
    closedText = new QLabel;
    AguaraStyle::setRole(closedText, AguaraStyle::Muted);
    layout->addWidget(closedText, 0, Qt::AlignHCenter);
    layout->addSpacing(8);
    closedButton = new QPushButton;
    closedButton->setObjectName(QStringLiteral("closedButton"));
    AguaraStyle::setRole(closedButton, AguaraStyle::Primary);
    layout->addWidget(closedButton, 0, Qt::AlignHCenter);
    layout->addStretch(3);
    connect(closedButton, &QPushButton::clicked, this, [this]() {
        const std::optional<CashSession> session = cash->currentSession();
        if (session)
            closeRegister();
        else
            openRegister();
    });
    return page;
}

QPushButton *SalesModule::addKeyButton(const QString &text, const char *name, void (SalesModule::*action)())
{
    auto button = new QPushButton(text);
    button->setObjectName(QLatin1String(name));
    button->setFocusPolicy(Qt::NoFocus);        // the cursor stays where the scanner writes
    connect(button, &QPushButton::clicked, this, action);
    return button;
}

QWidget *SalesModule::buildSalePage()
{
    auto page = new QWidget;
    auto layout = new QHBoxLayout(page);
    layout->setContentsMargins(24, 16, 24, 16);
    layout->setSpacing(16);

    auto left = new QVBoxLayout;
    left->setSpacing(10);
    entry = new QLineEdit;
    entry->setObjectName(QStringLiteral("saleEntry"));
    entry->setPlaceholderText(tr("Código, o cantidad*código (3*7840...). Texto: busca por descripción"));
    AguaraStyle::searchField(entry);
    entry->installEventFilter(this);
    left->addWidget(entry);

    table = makeSalesTable({ tr("Código"), tr("Descripción"), tr("Cantidad"), tr("Precio"), tr("Descuento"), tr("Importe") });
    table->setObjectName(QStringLiteral("saleLines"));
    table->setFocusPolicy(Qt::NoFocus);
    table->setColumnWidth(ColumnCode, table->fontMetrics().horizontalAdvance(QStringLiteral("00000000000000")) + 32);
    table->setColumnWidth(ColumnQuantity, 84);
    table->setColumnWidth(ColumnPrice, 84);
    table->setColumnWidth(ColumnDiscount, 92);
    table->setColumnWidth(ColumnAmount, 100);
    table->horizontalHeader()->setSectionResizeMode(ColumnDescription, QHeaderView::Stretch);
    left->addWidget(table, 1);

    message = new QLabel;
    message->setObjectName(QStringLiteral("saleMessage"));
    message->setWordWrap(true);
    message->setMargin(8);          // the banner's padding doesn't survive changing its role
    message->hide();
    left->addWidget(message);
    layout->addLayout(left, 1);

    auto side = new QVBoxLayout;
    side->setSpacing(10);
    auto card = new QFrame;
    AguaraStyle::setRole(card, AguaraStyle::Card);
    card->setFixedWidth(280);
    auto cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(16, 14, 16, 14);
    cardLayout->setSpacing(6);
    auto heading = new QLabel(tr("TOTAL"));
    AguaraStyle::setRole(heading, AguaraStyle::Section);
    cardLayout->addWidget(heading);
    totalLabel = new QLabel;
    totalLabel->setObjectName(QStringLiteral("saleTotal"));
    totalLabel->setStyleSheet(QStringLiteral("font-size: 30pt; font-weight: 600;"));
    totalLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    cardLayout->addWidget(totalLabel);
    auto figures = new QGridLayout;
    figures->setVerticalSpacing(3);
    subtotalLabel = figure(figures, 0, tr("Subtotal"));
    discountLabel = figure(figures, 1, tr("Descuentos"));
    discountLabel->setObjectName(QStringLiteral("saleDiscount"));
    vatLabel = figure(figures, 2, tr("IVA incluido"));
    countLabel = figure(figures, 3, tr("Artículos"));
    cardLayout->addLayout(figures);
    lastChange = new QLabel;
    lastChange->setObjectName(QStringLiteral("lastChange"));
    lastChange->setStyleSheet(QStringLiteral("font-size: 13pt; font-weight: 600;"));
    lastChange->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    lastChange->hide();
    cardLayout->addWidget(lastChange);
    side->addWidget(card);

    auto keys = new QGridLayout;
    keys->setSpacing(6);
    keys->addWidget(addKeyButton(tr("F2 Cantidad"), "keyQuantity", &SalesModule::changeQuantity), 0, 0);
    keys->addWidget(addKeyButton(tr("F3 Buscar"), "keySearch", &SalesModule::searchArticle), 0, 1);
    keys->addWidget(addKeyButton(tr("F4 Desc. línea"), "keyLineDiscount", &SalesModule::discountLine), 1, 0);
    keys->addWidget(addKeyButton(tr("F5 Desc. venta"), "keySaleDiscount", &SalesModule::discountSale), 1, 1);
    keys->addWidget(addKeyButton(tr("F6 En espera"), "keyHold", &SalesModule::holdSale), 2, 0);
    recallButton = addKeyButton(tr("F7 Recuperar"), "keyRecall", &SalesModule::recallSale);
    keys->addWidget(recallButton, 2, 1);
    keys->addWidget(addKeyButton(tr("Supr Quitar"), "keyRemove", &SalesModule::removeLine), 3, 0);
    keys->addWidget(addKeyButton(tr("Esc Cancelar"), "keyCancel", &SalesModule::cancelSale), 3, 1);
    side->addLayout(keys);
    side->addStretch();
    QPushButton *chargeButton = addKeyButton(tr("F12  Cobrar"), "keyCharge", &SalesModule::charge);
    AguaraStyle::setRole(chargeButton, AguaraStyle::Primary);
    chargeButton->setMinimumHeight(52);
    chargeButton->setStyleSheet(QStringLiteral("font-size: 14pt;"));
    side->addWidget(chargeButton);
    layout->addLayout(side);
    return page;
}

void SalesModule::showEvent(QShowEvent *event)
{
    AguaraWidget::showEvent(event);
    refreshSession();
}

bool SalesModule::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != entry || event->type() != QEvent::KeyPress)
        return AguaraWidget::eventFilter(watched, event);

    const auto keyEvent = static_cast<QKeyEvent *>(event);
    switch (keyEvent->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:  submitEntry(); return true;
    case Qt::Key_F2:     changeQuantity(); return true;
    case Qt::Key_F3:     searchArticle(); return true;
    case Qt::Key_F4:     discountLine(); return true;
    case Qt::Key_F5:     discountSale(); return true;
    case Qt::Key_F6:     holdSale(); return true;
    case Qt::Key_F7:     recallSale(); return true;
    case Qt::Key_F12:    charge(); return true;
    case Qt::Key_Up:
    case Qt::Key_Down: {
        const int row = currentLine() + (keyEvent->key() == Qt::Key_Down ? 1 : -1);
        if (row >= 0 && row < table->rowCount())
            table->selectRow(row);
        return true;
    }
    case Qt::Key_Delete:
        if (entry->text().isEmpty()) {
            removeLine();
            return true;
        }
        break;
    case Qt::Key_Escape:
        if (!entry->text().isEmpty())
            entry->clear();
        else
            cancelSale();
        return true;
    default:
        break;
    }
    return AguaraWidget::eventFilter(watched, event);
}

// --- what is shown -----------------------------------------------------------

void SalesModule::refreshSession()
{
    const CashRegister cashRegister = cash->currentRegister();
    const std::optional<CashSession> session = cash->currentSession();
    const bool selling = session && !session->isStale();

    if (selling) {
        subtitle->setText(tr("%1 · abierta por %2 a las %3").arg(cashRegister.name, session->openedBy,
                          session->opened.toString(QStringLiteral("HH:mm"))));
        pages->setCurrentIndex(1);
        recallButton->setText(tr("F7 Recuperar (%1)").arg(sales->heldSales().size()));
        entry->setFocus();
        return;
    }
    if (session) {
        subtitle->setText(tr("%1 · sesión pendiente").arg(cashRegister.name));
        closedTitle->setText(tr("Hay una caja del %1 sin cerrar").arg(session->opened.toString(QStringLiteral("dd/MM/yyyy"))));
        closedText->setText(tr("Hay que cerrarla antes de vender. Un supervisor debe autorizarlo."));
        closedButton->setText(tr("Cerrar esa caja"));
    } else {
        subtitle->setText(tr("%1 · cerrada").arg(cashRegister.name));
        closedTitle->setText(tr("La caja está cerrada"));
        closedText->setText(tr("Para vender, ábrala contando el fondo inicial."));
        closedButton->setText(tr("Abrir caja"));
    }
    pages->setCurrentIndex(0);
}

void SalesModule::renderSale()
{
    // a smaller sale can't keep a larger discount
    Money afterLineDiscounts = 0;
    for (const SaleLine &line : draft.lines)
        afterLineDiscounts += AguaraSale::lineGross(line) - line.discount;
    draft.totalDiscount = qMin(draft.totalDiscount, afterLineDiscounts);

    const int keep = currentLine();
    const SaleTotals totals = AguaraSale::totals(draft);
    table->setRowCount(draft.lines.size());
    for (int row = 0; row < draft.lines.size(); ++row) {
        const SaleLine &line = draft.lines.at(row);
        table->setItem(row, ColumnCode, new QTableWidgetItem(line.code));
        table->setItem(row, ColumnDescription, new QTableWidgetItem(line.description));
        table->setItem(row, ColumnQuantity, rightItem(AguaraQuantity::format(line.quantity, line.decimals)
                                                      + (line.unit == QLatin1String("un") ? QString() : QLatin1Char(' ') + line.unit)));
        table->setItem(row, ColumnPrice, rightItem(AguaraMoney::format(line.unitPrice)));
        table->setItem(row, ColumnDiscount, rightItem(line.discount ? AguaraMoney::format(line.discount) : QString()));
        table->setItem(row, ColumnAmount, rightItem(AguaraMoney::format(AguaraSale::lineGross(line) - line.discount)));
    }
    if (table->rowCount() > 0)
        table->selectRow(qBound(0, keep, table->rowCount() - 1));

    totalLabel->setText(AguaraMoney::format(totals.total));
    subtotalLabel->setText(AguaraMoney::format(totals.gross));
    discountLabel->setText(AguaraMoney::format(totals.discount));
    vatLabel->setText(AguaraMoney::format(totals.vat10 + totals.vat5));
    countLabel->setText(QString::number(draft.lines.size()));
}

void SalesModule::say(const char *role, const QString &text)
{
    message->setText(text);
    AguaraStyle::setRole(message, role);
    message->setVisible(!text.isEmpty());
}

/// Said in the page, not in a dialog: the next scan must not be lost.
void SalesModule::complain(const QString &text)
{
    QApplication::beep();
    say(AguaraStyle::Error, text);
}

int SalesModule::currentLine() const
{
    return table->currentRow();
}

// --- ringing up --------------------------------------------------------------

void SalesModule::submitEntry()
{
    QString text = entry->text().trimmed();
    if (text.isEmpty())
        return;
    entry->clear();

    bool explicitQuantity = false;
    Quantity quantity = AguaraQuantity::One;
    const int star = text.indexOf(QLatin1Char('*'));
    if (star > 0) {
        if (!AguaraQuantity::parse(text.left(star), &quantity) || quantity <= 0) {
            complain(tr("La cantidad \"%1\" no es válida.").arg(text.left(star)));
            return;
        }
        explicitQuantity = true;
        text = text.mid(star + 1).trimmed();
    }

    const ScanResult scan = inventory->resolveScan(text);
    if (scan.found) {
        addScan(scan, explicitQuantity, quantity);
        return;
    }
    bool isCode = !text.isEmpty();
    for (const QChar c : text)
        isCode = isCode && c.isDigit();
    if (isCode) {
        complain(tr("No hay ningún artículo con el código %1.").arg(text));
        return;
    }

    ArticleSearchDialog search(products, text, this);
    if (search.exec() == QDialog::Accepted) {
        const ScanResult found = inventory->resolveScan(search.code());
        if (found.found)
            addScan(found, explicitQuantity, quantity);
    }
    entry->setFocus();
}

void SalesModule::addScan(const ScanResult &scan, bool explicitQuantity, Quantity quantity)
{
    const Product &p = scan.product;
    if (p.price <= 0) {
        complain(tr("%1 no tiene precio.").arg(p.description));
        return;
    }
    const bool fromScale = scan.quantity != AguaraQuantity::One || scan.price != p.price;
    if (explicitQuantity && p.unit.decimals == 0 && quantity % AguaraQuantity::One != 0) {
        complain(tr("%1 se vende por unidad.").arg(p.description));
        return;
    }

    int row = -1;
    if (!fromScale && p.unit.decimals == 0) {       // same article again: one more on its line
        for (int i = 0; i < draft.lines.size(); ++i) {
            const SaleLine &line = draft.lines.at(i);
            if (line.code == p.code && line.discount == 0 && line.fixedAmount == 0)
                row = i;
        }
    }
    const Quantity added = fromScale ? scan.quantity : quantity;
    if (row >= 0) {
        draft.lines[row].quantity += added;
    } else {
        SaleLine line;
        line.code = p.code;
        line.description = p.description;
        line.unit = p.unit.abbreviation;
        line.decimals = p.unit.decimals;
        line.quantity = added;
        line.unitPrice = p.price;
        line.fixedAmount = scan.price != p.price ? scan.price : 0;
        line.vatRate = p.vatRate;
        draft.lines << line;
        row = draft.lines.size() - 1;
    }
    table->setRowCount(draft.lines.size());
    table->setCurrentCell(row, 0);
    renderSale();
    lastChange->hide();

    Quantity inSale = 0;
    for (const SaleLine &line : draft.lines) {
        if (line.code == p.code)
            inSale += line.quantity;
    }
    if (inSale > p.stock)
        say(AguaraStyle::Warning, tr("%1: el stock queda en %2. La venta sigue; revise el inventario.")
                .arg(p.description, AguaraQuantity::format(p.stock - inSale, p.unit.decimals)));
    else
        say(AguaraStyle::Info, QString());
}

void SalesModule::searchArticle()
{
    ArticleSearchDialog search(products, entry->text().trimmed(), this);
    if (search.exec() == QDialog::Accepted) {
        entry->clear();
        const ScanResult found = inventory->resolveScan(search.code());
        if (found.found)
            addScan(found, false, AguaraQuantity::One);
    }
    entry->setFocus();
}

void SalesModule::changeQuantity()
{
    const int row = currentLine();
    if (row < 0)
        return;
    SaleLine &line = draft.lines[row];
    QuantityDialog dialog(line, this);
    if (dialog.exec() == QDialog::Accepted) {
        line.quantity = dialog.quantity();
        line.fixedAmount = 0;           // the scale's price was for its own weight
        line.discount = qMin(line.discount, AguaraSale::lineGross(line));
        renderSale();
    }
    entry->setFocus();
}

void SalesModule::removeLine()
{
    const int row = currentLine();
    if (row < 0)
        return;
    const SaleLine line = draft.lines.takeAt(row);
    users->audit(QStringLiteral("venta.quitar_linea"), QStringLiteral("venta"), line.code, QString(),
                 tr("%1 x %2").arg(AguaraQuantity::format(line.quantity, line.decimals), line.description));
    renderSale();
    entry->setFocus();
}

/// Up to the role's limit a discount just happens; past it, a supervisor.
bool SalesModule::mayDiscount(Money discount, Money base, const QString &what)
{
    if (base <= 0 || discount * 100 <= base * users->discountLimit())
        return true;
    return users->authorize(QStringLiteral("ventas.descuento_libre"), what, this);
}

void SalesModule::discountLine()
{
    const int row = currentLine();
    if (row < 0)
        return;
    SaleLine &line = draft.lines[row];
    const Money gross = AguaraSale::lineGross(line);
    DiscountDialog dialog(tr("Descuento de línea"), line.description, gross, this);
    if (dialog.exec() == QDialog::Accepted) {
        const QString what = tr("Descuento de Gs. %1 en %2").arg(AguaraMoney::format(dialog.discount()), line.description);
        // the line's own limit, and the limit on the sale as a whole
        Money saleGross = 0, others = draft.totalDiscount;
        for (int i = 0; i < draft.lines.size(); ++i) {
            saleGross += AguaraSale::lineGross(draft.lines.at(i));
            if (i != row)
                others += draft.lines.at(i).discount;
        }
        const int limit = users->discountLimit();
        const bool withinLimit = dialog.discount() * 100 <= gross * limit
                                 && (dialog.discount() + others) * 100 <= saleGross * limit;
        if (withinLimit || users->authorize(QStringLiteral("ventas.descuento_libre"), what, this)) {
            line.discount = dialog.discount();
            renderSale();
        } else {
            complain(tr("El descuento supera lo permitido y no se autorizó."));
        }
    }
    entry->setFocus();
}

void SalesModule::discountSale()
{
    if (draft.lines.isEmpty())
        return;
    Money gross = 0, lineDiscounts = 0;
    for (const SaleLine &line : draft.lines) {
        gross += AguaraSale::lineGross(line);
        lineDiscounts += line.discount;
    }
    const Money base = gross - lineDiscounts;
    DiscountDialog dialog(tr("Descuento de la venta"), tr("Sobre Gs. %1").arg(AguaraMoney::format(base)), base, this);
    if (dialog.exec() == QDialog::Accepted) {
        // the limit is on all the discounts of the sale together
        const QString what = tr("Descuento de Gs. %1 en la venta").arg(AguaraMoney::format(dialog.discount()));
        if (mayDiscount(dialog.discount() + lineDiscounts, gross, what)) {
            draft.totalDiscount = dialog.discount();
            renderSale();
        } else {
            complain(tr("El descuento supera lo permitido y no se autorizó."));
        }
    }
    entry->setFocus();
}

void SalesModule::holdSale()
{
    if (draft.lines.isEmpty())
        return;
    if (!sales->holdSale(draft)) {
        complain(sales->lastError());
        return;
    }
    draft = SaleDraft();
    renderSale();
    refreshSession();
    say(AguaraStyle::Info, tr("Venta en espera. F7 para recuperarla."));
}

void SalesModule::recallSale()
{
    if (!draft.lines.isEmpty()) {
        complain(tr("Termine o ponga en espera la venta actual antes de recuperar otra."));
        return;
    }
    const QList<HeldSale> held = sales->heldSales();
    if (held.isEmpty()) {
        say(AguaraStyle::Info, tr("No hay ventas en espera."));
        return;
    }
    HeldSalesDialog dialog(held, this);
    if (dialog.exec() == QDialog::Accepted) {
        const std::optional<SaleDraft> taken = sales->takeHeldSale(dialog.heldId());
        if (taken) {
            draft = *taken;
            renderSale();
            say(AguaraStyle::Info, QString());
        } else {
            complain(sales->lastError());
        }
    }
    refreshSession();
}

void SalesModule::cancelSale()
{
    if (draft.lines.isEmpty())
        return;
    if (askConfirm(this, tr("Cancelar la venta"), tr("Se quitan todos los artículos de la venta."), tr("Cancelar la venta"))) {
        users->audit(QStringLiteral("venta.cancelar"), QStringLiteral("venta"), QString(), QString(),
                     tr("%1 líneas, Gs. %2").arg(draft.lines.size())
                         .arg(AguaraMoney::format(AguaraSale::totals(draft).total)));
        draft = SaleDraft();
        renderSale();
        say(AguaraStyle::Info, QString());
    }
    entry->setFocus();
}

void SalesModule::charge()
{
    if (draft.lines.isEmpty()) {
        complain(tr("La venta no tiene artículos."));
        return;
    }
    PaymentDialog dialog(AguaraSale::totals(draft).net, sales, users, this);
    if (dialog.exec() == QDialog::Accepted) {
        const qint64 id = sales->checkout(draft, dialog.payments());
        if (id == 0) {
            complain(sales->lastError());       // the sale stays on the screen
        } else {
            const std::optional<SaleRecord> record = sales->sale(id);
            draft = SaleDraft();
            renderSale();
            lastChange->setText(tr("Vuelto: Gs. %1").arg(AguaraMoney::format(dialog.change())));
            lastChange->setVisible(dialog.paidInCash());
            say(AguaraStyle::Success, tr("Venta %1 cobrada.").arg(
                record ? AguaraSale::formatNumber(record->registerId, record->number) : QString()));
        }
    }
    refreshSession();
}

// --- the register ------------------------------------------------------------

void SalesModule::openRegister()
{
    if (!users->authorize(QStringLiteral("caja.abrir"), tr("Abrir la caja"), this))
        return;
    CountDialog dialog(tr("Apertura de caja"), tr("Cuente el fondo inicial, billete por billete."),
                       tr("Abrir caja"), CashCount(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    if (!cash->openSession(dialog.count()))
        emit notify(ModuleMsg(QString(), QStringLiteral("caja"), tr("No se pudo abrir la caja"), cash->lastError(),
                              MsgPriority::PriorityHigh));
    say(AguaraStyle::Info, QString());
    lastChange->hide();
    refreshSession();
}

void SalesModule::closeRegister()
{
    const std::optional<CashSession> session = cash->currentSession();
    if (!session) {
        say(AguaraStyle::Info, tr("La caja ya está cerrada."));
        return;
    }
    if (!draft.lines.isEmpty()) {
        complain(tr("Termine, ponga en espera o cancele la venta actual antes de cerrar."));
        return;
    }
    const QString permission = session->isStale() ? QStringLiteral("caja.cerrar_olvidada") : QStringLiteral("caja.cerrar");
    if (!users->authorize(permission, tr("Cerrar la caja"), this))
        return;

    CashCount count;
    for (;;) {
        CountDialog dialog(tr("Arqueo de cierre"), tr("Cuente todo el efectivo del cajón, billete por billete."),
                           tr("Cerrar caja"), count, this);
        if (dialog.exec() != QDialog::Accepted)
            return;
        count = dialog.count();

        const CloseResult result = cash->closeSession(count);
        if (result == CloseResult::Closed)
            break;
        if (result == CloseResult::Failed) {
            complain(cash->lastError());
            return;
        }
        // Blind: it says that the count is wrong, never by how much.
        const MismatchChoice choice = askAfterMismatch(this);
        if (choice == MismatchChoice::Recount)
            continue;
        if (choice == MismatchChoice::Leave)
            return;
        if (!users->authorize(QStringLiteral("caja.cerrar_diferencia"), tr("Cerrar la caja con diferencia"), this))
            return;
        const QString authorizer = users->lastAuthorizer();
        const std::optional<CashSessionReport> report = cash->sessionReport(session->id);
        if (!report)
            return;
        DifferenceDialog difference(report->expectedCash, AguaraCash::countTotal(count), this);
        if (difference.exec() != QDialog::Accepted)
            return;
        if (!cash->closeWithDifference(count, difference.reason(), authorizer)) {
            complain(cash->lastError());
            return;
        }
        break;
    }

    refreshSession();
    say(AguaraStyle::Info, QString());
    lastChange->hide();
    if (askConfirm(this, tr("Caja cerrada"), tr("¿Ver e imprimir la planilla de cierre?"), tr("Ver planilla"))) {
        if (const std::optional<CashSessionReport> report = cash->sessionReport(session->id))
            previewClosingSheet(*report, this);
    }
}

void SalesModule::cashMovement()
{
    const std::optional<CashSession> session = cash->currentSession();
    if (!session || session->isStale()) {
        say(AguaraStyle::Info, tr("La caja no está abierta."));
        return;
    }
    if (!users->authorize(QStringLiteral("caja.movimientos"), tr("Registrar un movimiento de caja"), this))
        return;
    CashMovementDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        if (cash->addMovement(dialog.type(), dialog.amount(), dialog.reason()))
            say(AguaraStyle::Success, tr("Movimiento registrado: Gs. %1.").arg(AguaraMoney::format(dialog.amount())));
        else
            complain(cash->lastError());
    }
    entry->setFocus();
}

void SalesModule::showSessionSales()
{
    const std::optional<CashSession> session = cash->currentSession();
    if (!session) {
        say(AguaraStyle::Info, tr("La caja no está abierta."));
        return;
    }
    SessionSalesDialog dialog(sales, users, session->id, this);
    dialog.exec();
    entry->setFocus();
}

void SalesModule::showSessions()
{
    if (!users->authorize(QStringLiteral("caja.planillas"), tr("Ver planillas de cierre"), this))
        return;
    SessionsDialog dialog(cash, this);
    dialog.exec();
}

void SalesModule::renameRegister()
{
    if (!users->authorize(QStringLiteral("caja.configurar"), tr("Cambiar el nombre de la caja"), this))
        return;
    const QString name = askReason(this, tr("Nombre de esta caja"), tr("Actualmente: %1").arg(cash->currentRegister().name));
    if (name.isEmpty())
        return;
    if (!cash->renameRegister(name))
        complain(cash->lastError());
    refreshSession();
}
