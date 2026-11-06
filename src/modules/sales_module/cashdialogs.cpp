#include "cashdialogs.h"
#include "closingsheet.h"
#include "saledialogs.h"

#include <formcheck.h>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPrintPreviewDialog>
#include <QPrinter>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

CountDialog::CountDialog(const QString &title, const QString &subtitle, const QString &acceptText,
                         const CashCount &initial, QWidget *parent)
    : QDialog(parent)
{
    QVBoxLayout *layout = dialogHeader(this, title, subtitle);

    auto grid = new QGridLayout;
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(6);
    const auto addColumn = [&](const QString &heading, int column, Money from, Money to) {
        auto label = new QLabel(heading);
        AguaraStyle::setRole(label, AguaraStyle::Section);
        grid->addWidget(label, 0, column, 1, 3);
        int row = 1;
        for (Money value : AguaraCash::denominations()) {
            if (value > from || value < to)
                continue;
            auto name = new QLabel(AguaraMoney::format(value));
            name->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            auto spin = new QSpinBox;
            spin->setObjectName(QStringLiteral("countSpin_%1").arg(value));
            spin->setRange(0, 99999);
            spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
            spin->setAlignment(Qt::AlignRight);
            spin->setMinimumWidth(80);
            spin->installEventFilter(this);
            auto subtotal = new QLabel(QStringLiteral("0"));
            subtotal->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            subtotal->setMinimumWidth(90);
            AguaraStyle::setRole(subtotal, AguaraStyle::Muted);
            grid->addWidget(name, row, column);
            grid->addWidget(spin, row, column + 1);
            grid->addWidget(subtotal, row, column + 2);
            ++row;
            spins.insert(value, spin);
            subtotals.insert(value, subtotal);
            connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this, &CountDialog::update);
        }
    };
    addColumn(tr("BILLETES"), 0, 100000, 2000);
    addColumn(tr("MONEDAS"), 3, 1000, 50);
    grid->setColumnMinimumWidth(3, 90);
    layout->addLayout(grid);

    total = new QLabel;
    total->setObjectName(QStringLiteral("countTotal"));
    total->setStyleSheet(QStringLiteral("font-size: 14pt; font-weight: 600;"));
    total->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    layout->addWidget(total);
    layout->addWidget(acceptCancel(this, acceptText));

    for (const DenominationCount &d : initial) {
        if (QSpinBox *spin = spins.value(d.denomination))
            spin->setValue(d.count);
    }
    update();
    spins.value(100000)->setFocus();
    spins.value(100000)->selectAll();
}

CashCount CountDialog::count() const
{
    CashCount result;
    for (Money value : AguaraCash::denominations()) {
        DenominationCount d;
        d.denomination = value;
        d.count = spins.value(value)->value();
        if (d.count > 0)
            result << d;
    }
    return result;
}

void CountDialog::update()
{
    for (auto it = spins.cbegin(); it != spins.cend(); ++it)
        subtotals.value(it.key())->setText(AguaraMoney::format(it.key() * it.value()->value()));
    total->setText(tr("Total: %1").arg(AguaraMoney::formatWithSymbol(AguaraCash::countTotal(count()))));
}

/// Enter goes to the next denomination, and from the last one to the button:
/// a count can't be accepted halfway by accident.
bool CountDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent *>(event)->key();
        if (key == Qt::Key_Return || key == Qt::Key_Enter) {
            focusNextChild();
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}

// --- CashMovementDialog ------------------------------------------------------

CashMovementDialog::CashMovementDialog(QWidget *parent)
    : QDialog(parent)
{
    QVBoxLayout *layout = dialogHeader(this, tr("Movimiento de caja"), tr("Dinero que entra o sale del cajón sin ser una venta."));
    auto form = new QFormLayout;
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(8);
    kind = new QComboBox;
    kind->setObjectName(QStringLiteral("movementType"));
    kind->addItem(tr("Retiro parcial (al cofre)"));
    kind->addItem(tr("Egreso (pago a proveedor, gasto)"));
    kind->addItem(tr("Ingreso (cambio, reposición)"));
    amountEdit = new QLineEdit;
    amountEdit->setObjectName(QStringLiteral("movementAmount"));
    amountEdit->setAlignment(Qt::AlignRight);
    reasonEdit = new QLineEdit;
    reasonEdit->setObjectName(QStringLiteral("movementReason"));
    form->addRow(tr("Tipo:"), kind);
    form->addRow(AguaraForm::required(tr("Monto:")), amountEdit);
    form->addRow(AguaraForm::required(tr("Motivo:")), reasonEdit);
    layout->addLayout(form);
    layout->addWidget(AguaraForm::requiredNote());
    error = AguaraForm::errorLabel(QStringLiteral("movementError"));
    layout->addWidget(error);
    layout->addWidget(acceptCancel(this, tr("Registrar")));
    setMinimumWidth(420);
    amountEdit->setFocus();
}

const char *CashMovementDialog::type() const
{
    switch (kind->currentIndex()) {
    case 1:  return CashMovementType::Out;
    case 2:  return CashMovementType::In;
    default: return CashMovementType::Withdrawal;
    }
}

QString CashMovementDialog::reason() const
{
    return reasonEdit->text().trimmed();
}

void CashMovementDialog::accept()
{
    Money amount = 0;
    if (!AguaraMoney::parse(amountEdit->text(), &amount) || amount <= 0) {
        AguaraForm::showError(error, tr("Escriba un monto mayor que cero."), amountEdit);
        return;
    }
    if (reason().isEmpty()) {
        AguaraForm::showError(error, tr("Indique el motivo del movimiento."), reasonEdit);
        return;
    }
    value = amount;
    QDialog::accept();
}

// --- the count didn't match --------------------------------------------------

MismatchChoice askAfterMismatch(QWidget *parent)
{
    QDialog dialog(parent);
    QVBoxLayout *layout = dialogHeader(&dialog, QObject::tr("El conteo no coincide"),
        QObject::tr("Vuelva a contar. Si el conteo es correcto, un supervisor puede cerrar con la diferencia."));
    MismatchChoice choice = MismatchChoice::Leave;
    auto row = new QHBoxLayout;
    row->setSpacing(8);
    const auto add = [&](const QString &text, const char *name, MismatchChoice value) {
        auto button = new QPushButton(text);
        button->setObjectName(QLatin1String(name));
        QObject::connect(button, &QPushButton::clicked, &dialog, [&dialog, &choice, value]() {
            choice = value;
            dialog.accept();
        });
        row->addWidget(button);
        return button;
    };
    QPushButton *recount = add(QObject::tr("Volver a contar"), "mismatchRecount", MismatchChoice::Recount);
    recount->setDefault(true);
    AguaraStyle::setRole(recount, AguaraStyle::Primary);
    add(QObject::tr("Llamar a un supervisor"), "mismatchSupervisor", MismatchChoice::Supervisor);
    add(QObject::tr("Dejar la caja abierta"), "mismatchLeave", MismatchChoice::Leave);
    layout->addLayout(row);
    dialog.exec();
    return choice;
}

// --- DifferenceDialog --------------------------------------------------------

DifferenceDialog::DifferenceDialog(Money expected, Money counted, QWidget *parent)
    : QDialog(parent)
{
    QVBoxLayout *layout = dialogHeader(this, tr("Cerrar con diferencia"),
                                       tr("La diferencia queda registrada y aparece en los reportes."));
    auto form = new QFormLayout;
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(6);
    const Money difference = counted - expected;
    auto differenceLabel = new QLabel(AguaraMoney::formatWithSymbol(difference));
    differenceLabel->setObjectName(QStringLiteral("differenceAmount"));
    differenceLabel->setStyleSheet(QStringLiteral("font-weight: 600;"));
    form->addRow(tr("Esperado:"), new QLabel(AguaraMoney::formatWithSymbol(expected)));
    form->addRow(tr("Contado:"), new QLabel(AguaraMoney::formatWithSymbol(counted)));
    form->addRow(difference < 0 ? tr("Faltante:") : tr("Sobrante:"), differenceLabel);
    layout->addLayout(form);
    reasonEdit = new QLineEdit;
    reasonEdit->setObjectName(QStringLiteral("differenceReason"));
    reasonEdit->setPlaceholderText(tr("Motivo de la diferencia"));
    layout->addWidget(reasonEdit);
    QDialogButtonBox *buttons = acceptCancel(this, tr("Cerrar la caja"));
    QPushButton *ok = buttons->button(QDialogButtonBox::Ok);
    ok->setEnabled(false);
    connect(reasonEdit, &QLineEdit::textChanged, ok, [ok](const QString &t) { ok->setEnabled(!t.trimmed().isEmpty()); });
    layout->addWidget(buttons);
    setMinimumWidth(400);
    reasonEdit->setFocus();
}

QString DifferenceDialog::reason() const
{
    return reasonEdit->text().trimmed();
}

// --- SessionsDialog ----------------------------------------------------------

SessionsDialog::SessionsDialog(CashService *cash, PrintService *print, QWidget *parent)
    : QDialog(parent), cash(cash), print(print)
{
    QVBoxLayout *layout = dialogHeader(this, tr("Sesiones de caja"), tr("Elija una sesión cerrada para ver o reimprimir su planilla."));
    // closed ones only: the figures of an open session would give the count away
    for (const CashSession &s : cash->sessions()) {
        if (!s.open)
            list << s;
    }
    table = makeSalesTable({ tr("Caja"), tr("Apertura"), tr("Cierre"), tr("Cerró"), tr("Contado"), tr("Diferencia") });
    table->setObjectName(QStringLiteral("sessionsTable"));
    table->setRowCount(list.size());
    for (int row = 0; row < list.size(); ++row) {
        const CashSession &s = list.at(row);
        table->setItem(row, 0, new QTableWidgetItem(s.registerName));
        table->setItem(row, 1, new QTableWidgetItem(s.opened.toString(QStringLiteral("dd/MM/yyyy HH:mm"))));
        table->setItem(row, 2, new QTableWidgetItem(s.closed.toString(QStringLiteral("dd/MM/yyyy HH:mm"))));
        table->setItem(row, 3, new QTableWidgetItem(s.closedBy));
        table->setItem(row, 4, rightItem(AguaraMoney::format(s.counted)));
        table->setItem(row, 5, rightItem(AguaraMoney::format(s.difference)));
    }
    table->resizeColumnsToContents();
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    if (!list.isEmpty())
        table->selectRow(0);
    layout->addWidget(table, 1);

    QDialogButtonBox *buttons = acceptCancel(this, tr("Ver planilla"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cerrar"));
    disconnect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::accepted, this, &SessionsDialog::printSelected);
    layout->addWidget(buttons);
    resize(760, 460);
}

void SessionsDialog::printSelected()
{
    const int row = table->currentRow();
    if (row < 0 || row >= list.size())
        return;
    if (const std::optional<CashSessionReport> report = cash->sessionReport(list.at(row).id))
        printClosingSheet(*report, print, this);
}

void printClosingSheet(const CashSessionReport &report, PrintService *print, QWidget *parent)
{
    if (print->printerSettings().kind == PrinterKind::Thermal && print->printReceipt(ClosingSheet::receipt(report)))
        return;

    QPrinter printer(QPrinter::HighResolution);
    printer.setPageSize(QPageSize(QPageSize::A4));
    QPrintPreviewDialog preview(&printer, parent);
    preview.setWindowTitle(QObject::tr("Planilla de cierre"));
    preview.resize(820, 900);
    QObject::connect(&preview, &QPrintPreviewDialog::paintRequested, &preview,
                     [&report](QPrinter *p) { ClosingSheet::print(p, report); });
    preview.exec();
}
