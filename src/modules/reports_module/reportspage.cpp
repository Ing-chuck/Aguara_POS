#include "reportspage.h"

#include <formcheck.h>
#include <QComboBox>
#include <QDateEdit>
#include <QFileDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPrintPreviewDialog>
#include <QPrinter>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTableWidget>
#include <QVBoxLayout>

ReportsPage::ReportsPage(ReportService *reports, UserService *users, SalesService *sales, QWidget *parent)
    : AguaraWidget(parent), reports(reports), users(users), sales(sales)
{
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto header = new QWidget;
    AguaraStyle::setRole(header, AguaraStyle::PageHeader);
    header->setAttribute(Qt::WA_StyledBackground);
    auto headerRow = new QHBoxLayout(header);
    headerRow->setContentsMargins(24, 14, 24, 14);
    auto heading = new QLabel(tr("Reportes"));
    AguaraStyle::setRole(heading, AguaraStyle::Title);
    headerRow->addWidget(heading, 1);
    csvButton = new QPushButton(tr("Exportar CSV"));
    csvButton->setObjectName(QStringLiteral("reportCsv"));
    pdfButton = new QPushButton(tr("Guardar PDF"));
    pdfButton->setObjectName(QStringLiteral("reportPdf"));
    printButton = new QPushButton(tr("Imprimir"));
    printButton->setObjectName(QStringLiteral("reportPrint"));
    for (QPushButton *button : { csvButton, pdfButton, printButton }) {
        button->setEnabled(false);
        headerRow->addWidget(button);
    }
    layout->addWidget(header);

    auto body = new QHBoxLayout;
    body->setContentsMargins(24, 16, 24, 16);
    body->setSpacing(16);

    list = new QListWidget;
    list->setObjectName(QStringLiteral("reportList"));
    list->setFixedWidth(240);
    body->addWidget(list);

    auto right = new QVBoxLayout;
    right->setSpacing(10);
    title = new QLabel;
    title->setObjectName(QStringLiteral("reportTitle"));
    AguaraStyle::setRole(title, AguaraStyle::Title);
    right->addWidget(title);

    auto filters = new QHBoxLayout;
    filters->setSpacing(8);
    datesRow = new QWidget;
    auto dates = new QHBoxLayout(datesRow);
    dates->setContentsMargins(0, 0, 0, 0);
    dates->setSpacing(6);
    from = new QDateEdit(QDate::currentDate());
    from->setObjectName(QStringLiteral("reportFrom"));
    to = new QDateEdit(QDate::currentDate());
    to->setObjectName(QStringLiteral("reportTo"));
    for (QDateEdit *edit : { from, to }) {
        edit->setCalendarPopup(true);
        edit->setDisplayFormat(QStringLiteral("dd/MM/yyyy"));
        edit->setMinimumWidth(130);
    }
    auto today = new QPushButton(tr("Hoy"));
    today->setObjectName(QStringLiteral("reportToday"));
    auto month = new QPushButton(tr("Este mes"));
    month->setObjectName(QStringLiteral("reportMonth"));
    dates->addWidget(new QLabel(tr("Del")));
    dates->addWidget(from);
    dates->addWidget(new QLabel(tr("al")));
    dates->addWidget(to);
    dates->addWidget(today);
    dates->addWidget(month);
    filters->addWidget(datesRow);
    filters->addStretch();
    auto run = new QPushButton(tr("Ver"));
    run->setObjectName(QStringLiteral("reportRun"));
    AguaraStyle::setRole(run, AguaraStyle::Primary);
    filters->addWidget(run);
    right->addLayout(filters);

    auto choices = new QHBoxLayout;
    choices->setSpacing(8);
    AguaraForm::keepInOrder(from, to);
    cashRegister = new QComboBox;
    cashRegister->setObjectName(QStringLiteral("reportRegister"));
    user = new QComboBox;
    user->setObjectName(QStringLiteral("reportUser"));
    category = new QComboBox;
    category->setObjectName(QStringLiteral("reportCategory"));
    choices->addWidget(cashRegister);
    choices->addWidget(user);
    choices->addWidget(category);
    daysLabel = new QLabel(tr("Días:"));
    choices->addWidget(daysLabel);
    days = new QSpinBox;
    days->setObjectName(QStringLiteral("reportDays"));
    days->setRange(1, 3650);
    days->setValue(30);
    choices->addWidget(days);
    text = new QLineEdit;
    text->setObjectName(QStringLiteral("reportText"));
    choices->addWidget(text, 1);
    choices->addStretch();
    right->addLayout(choices);

    subtitle = new QLabel;
    subtitle->setObjectName(QStringLiteral("reportSubtitle"));
    AguaraStyle::setRole(subtitle, AguaraStyle::Muted);
    right->addWidget(subtitle);

    table = new QTableWidget;
    table->setObjectName(QStringLiteral("reportTable"));
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setAlternatingRowColors(true);
    table->setShowGrid(false);
    table->setWordWrap(false);
    table->verticalHeader()->hide();
    table->horizontalHeader()->setHighlightSections(false);
    table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    right->addWidget(table, 1);

    note = new QLabel;
    note->setObjectName(QStringLiteral("reportNote"));
    note->setWordWrap(true);
    AguaraStyle::setRole(note, AguaraStyle::Muted);
    right->addWidget(note);
    problem = new QLabel;
    problem->setObjectName(QStringLiteral("reportProblem"));
    problem->setWordWrap(true);
    AguaraStyle::setRole(problem, AguaraStyle::Error);
    problem->hide();
    right->addWidget(problem);
    body->addLayout(right, 1);
    layout->addLayout(body, 1);

    connect(list, &QListWidget::currentRowChanged, this, &ReportsPage::select);
    connect(run, &QPushButton::clicked, this, &ReportsPage::execute);
    connect(text, &QLineEdit::returnPressed, this, &ReportsPage::execute);
    connect(today, &QPushButton::clicked, this, [this]() { setPeriod(QDate::currentDate(), QDate::currentDate()); });
    connect(month, &QPushButton::clicked, this, [this]() {
        const QDate now = QDate::currentDate();
        setPeriod(QDate(now.year(), now.month(), 1), now);
    });
    connect(csvButton, &QPushButton::clicked, this, &ReportsPage::exportCsv);
    connect(pdfButton, &QPushButton::clicked, this, &ReportsPage::exportPdf);
    connect(printButton, &QPushButton::clicked, this, &ReportsPage::printReport);

    fillChoices();
    fillList();
}

void ReportsPage::showEvent(QShowEvent *event)
{
    AguaraWidget::showEvent(event);
    fillChoices();              // a new register or category, since the page opened
}

/// By group. Only what this user may see: the headings can't be chosen.
void ReportsPage::fillList()
{
    definitions.clear();
    list->clear();
    QStringList groups;
    const QList<ReportDefinition> all = reports->reports();
    for (const ReportDefinition &r : all) {
        if (!groups.contains(r.group))
            groups << r.group;
    }
    for (const QString &group : qAsConst(groups)) {
        bool headed = false;
        for (const ReportDefinition &r : all) {
            if (r.group != group || (!r.permission.isEmpty() && !users->can(r.permission)))
                continue;
            if (!headed) {
                auto heading = new QListWidgetItem(group.toUpper());
                heading->setFlags(Qt::NoItemFlags);
                heading->setData(Qt::UserRole, -1);
                list->addItem(heading);
                headed = true;
            }
            auto item = new QListWidgetItem(r.title);
            item->setData(Qt::UserRole, definitions.size());
            list->addItem(item);
            definitions << r;
        }
    }
    if (list->count() > 1)
        list->setCurrentRow(1);
}

void ReportsPage::fillChoices()
{
    const auto refill = [](QComboBox *box, const QString &all, const QList<ReportChoice> &choices, bool byName) {
        const QVariant keep = box->currentData();
        box->clear();
        box->addItem(all, byName ? QVariant(QString()) : QVariant(qint64(0)));
        for (const ReportChoice &c : choices)
            box->addItem(c.name, byName ? QVariant(c.name) : QVariant(c.id));
        box->setCurrentIndex(qMax(0, box->findData(keep)));
    };
    refill(cashRegister, tr("Todas las cajas"), reports->registers(), false);
    refill(user, tr("Todos los usuarios"), reports->sellers(), true);
    refill(category, tr("Todos los rubros"), reports->categories(), false);
}

const ReportDefinition *ReportsPage::current() const
{
    const QListWidgetItem *item = list->currentItem();
    const int index = item ? item->data(Qt::UserRole).toInt() : -1;
    return index >= 0 && index < definitions.size() ? &definitions.at(index) : nullptr;
}

/// Show what this report asks for. It runs if it can without being told more.
void ReportsPage::select()
{
    const ReportDefinition *report = current();
    if (!report)
        return;
    title->setText(report->title);
    const int f = report->filters;
    datesRow->setVisible(f & ReportFilters::Dates);
    cashRegister->setVisible(f & ReportFilters::Register);
    user->setVisible(f & ReportFilters::User);
    category->setVisible(f & ReportFilters::Category);
    days->setVisible(f & ReportFilters::Days);
    daysLabel->setVisible(f & ReportFilters::Days);
    text->setVisible(f & ReportFilters::Text);
    text->setPlaceholderText(report->textLabel);
    if (!(f & ReportFilters::Text) || report->id != QLatin1String("stock.kardex")) {
        execute();
    } else {
        result.reset();
        showResult();
        text->setFocus();
    }
}

void ReportsPage::setPeriod(const QDate &first, const QDate &last)
{
    from->setDate(first);
    to->setDate(last);
    execute();
}

void ReportsPage::showProblem(const QString &message)
{
    problem->setText(message);
    problem->setVisible(!message.isEmpty());
}

void ReportsPage::execute()
{
    const ReportDefinition *report = current();
    if (!report)
        return;
    ReportFilter filter;
    if (report->filters & ReportFilters::Dates) {
        filter.from = from->date();
        filter.to = to->date();
    }
    if (report->filters & ReportFilters::Register)
        filter.registerId = cashRegister->currentData().toLongLong();
    if (report->filters & ReportFilters::User)
        filter.user = user->currentData().toString();
    if (report->filters & ReportFilters::Category)
        filter.categoryId = category->currentData().toLongLong();
    if (report->filters & ReportFilters::Days)
        filter.days = days->value();
    if (report->filters & ReportFilters::Text)
        filter.text = text->text().trimmed();

    result = reports->run(report->id, filter);
    showProblem(result ? QString() : reports->lastError());
    showResult();
}

void ReportsPage::showResult()
{
    const bool has = result.has_value();
    for (QPushButton *button : { csvButton, pdfButton, printButton })
        button->setEnabled(has);
    if (!has) {
        table->clear();
        table->setRowCount(0);
        table->setColumnCount(0);
        subtitle->clear();
        note->clear();
        return;
    }

    const int pending = sales->pendingSales().size();
    subtitle->setText(result->subtitle);
    note->setText(pending > 0 ? tr("%1 ventas de este equipo todavía no llegaron al servidor y no están aquí. %2").arg(pending).arg(result->note)
                              : result->note);

    table->clear();
    table->setColumnCount(result->columns.size());
    table->setRowCount(result->rows.size() + (result->totals.isEmpty() ? 0 : 1));
    QStringList headers;
    for (const ReportColumn &column : result->columns)
        headers << column.title;
    table->setHorizontalHeaderLabels(headers);

    const auto put = [this](int row, const QVector<QVariant> &values, bool strong) {
        for (int c = 0; c < result->columns.size(); ++c) {
            const ReportCell kind = result->columns.at(c).kind;
            auto item = new QTableWidgetItem(c < values.size() ? AguaraReport::formatCell(kind, values.at(c)) : QString());
            if (kind == ReportCell::Money || kind == ReportCell::Integer || kind == ReportCell::Quantity || kind == ReportCell::Percent)
                item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            if (strong) {
                QFont font = item->font();
                font.setBold(true);
                item->setFont(font);
            }
            table->setItem(row, c, item);
        }
    };
    for (int row = 0; row < result->rows.size(); ++row)
        put(row, result->rows.at(row), false);
    if (!result->totals.isEmpty())
        put(result->rows.size(), result->totals, true);
    table->resizeColumnsToContents();
    table->horizontalHeader()->setStretchLastSection(true);
}

void ReportsPage::exportCsv()
{
    const QString suggested = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                              + QLatin1Char('/') + result->title + QStringLiteral(".csv");
    const QString file = QFileDialog::getSaveFileName(this, tr("Exportar a CSV"), suggested, tr("Planilla CSV (*.csv)"));
    if (!file.isEmpty() && !reports->exportCsv(*result, file))
        showProblem(reports->lastError());
}

void ReportsPage::exportPdf()
{
    const QString suggested = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                              + QLatin1Char('/') + result->title + QStringLiteral(".pdf");
    const QString file = QFileDialog::getSaveFileName(this, tr("Guardar PDF"), suggested, tr("Documento PDF (*.pdf)"));
    if (file.isEmpty())
        return;
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);     // a long report takes some seconds
    const bool saved = reports->exportPdf(*result, file);
    QGuiApplication::restoreOverrideCursor();
    if (!saved)
        showProblem(reports->lastError());
}

void ReportsPage::printReport()
{
    QPrinter printer(QPrinter::HighResolution);
    QPrintPreviewDialog preview(&printer, this);
    preview.setWindowTitle(result->title);
    preview.resize(900, 760);
    connect(&preview, &QPrintPreviewDialog::paintRequested, &preview,
            [this](QPrinter *p) { reports->print(*result, p); });
    preview.exec();
}
