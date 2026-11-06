#ifndef LIVESEARCH_H
#define LIVESEARCH_H

#include "aguara_global.h"

#include <QList>
#include <QObject>
#include <QSortFilterProxyModel>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

class ProductService;
class QLineEdit;
class QListWidget;
class QTableWidget;
class QTimer;

/// Searching as you type, the same way everywhere: without caring about
/// capitals or accents, and with the words in any order. "azucar kilo" finds
/// "AZÚCAR BLANCA 1 KILO".
namespace AguaraSearch {

AGUARA_EXPORT QString normalize(const QString &text);
AGUARA_EXPORT bool matches(const QString &query, const QString &text);

/// Hides the rows of a table that don't match.
AGUARA_EXPORT void filterTable(QTableWidget *table, const QString &query);

/// Runs `apply` once the typing pauses. Stop the timer and call it directly
/// when the result is needed now (an Enter, say).
AGUARA_EXPORT QTimer *afterTyping(QLineEdit *edit, const std::function<void()> &apply);

} // namespace AguaraSearch

/// Shows the rows of a model that match the search. The text of each row is
/// kept ready, so filtering a whole catalog on each key costs next to nothing,
/// even on an old till.
class AGUARA_EXPORT SearchProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT
public:
    SearchProxyModel(const QList<int> &columns, QObject *parent);

    void setSourceModel(QAbstractItemModel *model) override;
    void setSearchText(const QString &text);

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;

private:
    QList<int> columns;
    QStringList words;
    mutable QVector<QString> rows;
};

/// A list under a field, suggesting what the typing could mean. Up and Down
/// walk it, Enter or a click choose, Esc closes it. Unless a row was walked
/// to, Enter still belongs to the field.
///
/// Made for fields a scanner also writes to: it waits for a pause in the
/// typing, and an Enter cancels the wait, so a scan never opens it. It doesn't
/// take the focus either: the cursor stays in the field.
class AGUARA_EXPORT SuggestionBox : public QObject
{
    Q_OBJECT
public:
    struct Item {
        QString key;        ///< what `chosen` gives back
        QString text;       ///< what the list shows
    };
    using Source = std::function<QList<Item>(const QString &text)>;

    SuggestionBox(QLineEdit *edit, const Source &source);

    /// Articles by code, description or brand, with their price and stock.
    /// Only for text with a letter in it: digits are a code being scanned.
    static Source articles(ProductService *products, QObject *owner);

signals:
    void chosen(const QString &key);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void update();
    void close();
    void choose(int row);

    QLineEdit *edit;
    Source source;
    QListWidget *list;
    QTimer *timer;
    QList<Item> items;
};

#endif // LIVESEARCH_H
