#include "livesearch.h"
#include "productservice.h"

#include <QAbstractItemModel>
#include <QEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTableWidget>
#include <QTimer>

#include <algorithm>

namespace {

const int kPauseMs = 150;       // longer than a scan, shorter than anyone notices
const int kMostSuggestions = 8;

QStringList wordsOf(const QString &query)
{
    return AguaraSearch::normalize(query).split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

bool hasAllWords(const QString &normalized, const QStringList &words)
{
    return std::all_of(words.cbegin(), words.cend(),
                       [&normalized](const QString &word) { return normalized.contains(word); });
}

} // namespace

namespace AguaraSearch {

QString normalize(const QString &text)
{
    const QString decomposed = text.simplified().normalized(QString::NormalizationForm_D);
    QString result;
    result.reserve(decomposed.size());
    for (const QChar c : decomposed) {
        if (c.category() != QChar::Mark_NonSpacing)     // what is left of an accent
            result += c.toLower();
    }
    return result;
}

bool matches(const QString &query, const QString &text)
{
    return hasAllWords(normalize(text), wordsOf(query));
}

void filterTable(QTableWidget *table, const QString &query)
{
    const QStringList words = wordsOf(query);
    for (int row = 0; row < table->rowCount(); ++row) {
        QStringList cells;
        for (int column = 0; column < table->columnCount(); ++column) {
            if (const QTableWidgetItem *item = table->item(row, column))
                cells << item->text();
        }
        table->setRowHidden(row, !hasAllWords(normalize(cells.join(QLatin1Char(' '))), words));
    }
}

QTimer *afterTyping(QLineEdit *edit, const std::function<void()> &apply)
{
    auto timer = new QTimer(edit);
    timer->setSingleShot(true);
    timer->setInterval(kPauseMs);
    QObject::connect(timer, &QTimer::timeout, edit, apply);
    QObject::connect(edit, &QLineEdit::textChanged, timer, QOverload<>::of(&QTimer::start));
    return timer;
}

} // namespace AguaraSearch

// --- SearchProxyModel ----------------------------------------------------------

SearchProxyModel::SearchProxyModel(const QList<int> &columns, QObject *parent)
    : QSortFilterProxyModel(parent), columns(columns)
{
}

void SearchProxyModel::setSourceModel(QAbstractItemModel *model)
{
    // before the proxy hears of it: it filters again as soon as it does
    const auto forget = [this]() { rows.clear(); };
    connect(model, &QAbstractItemModel::modelAboutToBeReset, this, forget);
    connect(model, &QAbstractItemModel::layoutAboutToBeChanged, this, forget);
    connect(model, &QAbstractItemModel::rowsAboutToBeInserted, this, forget);
    connect(model, &QAbstractItemModel::rowsAboutToBeRemoved, this, forget);
    connect(model, &QAbstractItemModel::dataChanged, this, forget);
    rows.clear();
    QSortFilterProxyModel::setSourceModel(model);
}

void SearchProxyModel::setSearchText(const QString &text)
{
    const QStringList wanted = wordsOf(text);
    if (wanted == words)
        return;
    words = wanted;
    invalidateFilter();
}

bool SearchProxyModel::filterAcceptsRow(int row, const QModelIndex &parent) const
{
    if (words.isEmpty())
        return true;
    if (rows.size() != sourceModel()->rowCount(parent))
        rows = QVector<QString>(sourceModel()->rowCount(parent));
    QString &text = rows[row];
    if (text.isNull()) {
        QStringList cells;
        for (int column : columns)
            cells << sourceModel()->index(row, column, parent).data().toString();
        text = AguaraSearch::normalize(cells.join(QLatin1Char(' ')));
        if (text.isNull())
            text = QStringLiteral("");
    }
    return hasAllWords(text, words);
}

// --- SuggestionBox -------------------------------------------------------------

SuggestionBox::SuggestionBox(QLineEdit *edit, const Source &source)
    : QObject(edit), edit(edit), source(source)
{
    list = new QListWidget(edit);
    list->setObjectName(QStringLiteral("suggestions"));
    list->setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint);
    list->setAttribute(Qt::WA_ShowWithoutActivating);
    list->setFocusPolicy(Qt::NoFocus);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setUniformItemSizes(true);
    connect(list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) { choose(list->row(item)); });

    timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(kPauseMs);
    connect(timer, &QTimer::timeout, this, &SuggestionBox::update);
    connect(edit, &QLineEdit::textEdited, this, [this]() {
        close();                    // what is on it no longer matches the text
        timer->start();
    });
    edit->installEventFilter(this);
}

SuggestionBox::Source SuggestionBox::articles(ProductService *products, QObject *owner)
{
    auto proxy = new SearchProxyModel({ int(ProductColumn::Code), int(ProductColumn::Description), int(ProductColumn::Brand) }, owner);
    return [products, proxy](const QString &text) {
        QList<Item> found;
        const bool hasLetter = std::any_of(text.cbegin(), text.cend(), [](QChar c) { return c.isLetter(); });
        if (text.trimmed().size() < 2 || !hasLetter)
            return found;
        if (!proxy->sourceModel())
            proxy->setSourceModel(products->createProductTableModel(proxy));
        proxy->setSearchText(text);
        const int shown = qMin(proxy->rowCount(), kMostSuggestions);
        for (int row = 0; row < shown; ++row) {
            const auto cell = [&](ProductColumn column) { return proxy->index(row, int(column)).data().toString(); };
            found << Item{ cell(ProductColumn::Code),
                           QObject::tr("%1  ·  Gs. %2  ·  stock %3")
                               .arg(cell(ProductColumn::Description), cell(ProductColumn::Price), cell(ProductColumn::Stock)) };
        }
        return found;
    };
}

void SuggestionBox::update()
{
    items = edit->hasFocus() ? source(edit->text()) : QList<Item>();
    if (items.isEmpty()) {
        close();
        return;
    }
    list->clear();
    for (const Item &item : qAsConst(items))
        list->addItem(item.text);
    list->setCurrentRow(-1);
    list->setFixedSize(edit->width(), list->sizeHintForRow(0) * items.size() + 2 * list->frameWidth());
    list->move(edit->mapToGlobal(QPoint(0, edit->height() + 2)));
    list->show();
}

void SuggestionBox::close()
{
    timer->stop();
    list->hide();
}

void SuggestionBox::choose(int row)
{
    const QString key = items.at(row).key;
    close();
    emit chosen(key);
}

/// Runs before the page's own handling of these keys, which only sees them
/// while the list is closed.
bool SuggestionBox::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != edit)
        return QObject::eventFilter(watched, event);
    if (event->type() == QEvent::FocusOut || event->type() == QEvent::Hide) {
        close();
        return false;
    }
    if (event->type() != QEvent::KeyPress)
        return false;

    const int key = static_cast<QKeyEvent *>(event)->key();
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        timer->stop();              // the end of a scan, or a search on purpose
        const int row = list->isVisible() ? list->currentRow() : -1;
        list->hide();
        if (row < 0)
            return false;
        choose(row);
        return true;
    }
    if (!list->isVisible())
        return false;

    switch (key) {
    case Qt::Key_Escape:
        close();
        return true;
    case Qt::Key_Down:
        list->setCurrentRow(qMin(list->currentRow() + 1, list->count() - 1));
        return true;
    case Qt::Key_Up:
        list->setCurrentRow(qMax(list->currentRow() - 1, -1));
        return true;
    default:
        return false;
    }
}
