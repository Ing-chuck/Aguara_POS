#ifndef CASHDIALOGS_H
#define CASHDIALOGS_H

#include <interfaces.h>

#include <QDialog>
#include <QHash>

class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QTableWidget;

/// Count notes and coins, at opening and closing. It never shows what is
/// expected: the count at closing is blind.
class CountDialog : public QDialog
{
    Q_OBJECT
public:
    CountDialog(const QString &title, const QString &subtitle, const QString &acceptText,
                const CashCount &initial, QWidget *parent);
    CashCount count() const;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void update();

    QHash<Money, QSpinBox *> spins;
    QHash<Money, QLabel *> subtotals;
    QLabel *total;
};

/// Money in or out of the drawer outside a sale.
class CashMovementDialog : public QDialog
{
    Q_OBJECT
public:
    explicit CashMovementDialog(QWidget *parent);
    const char *type() const;
    Money amount() const { return value; }
    QString reason() const;
    void accept() override;

private:
    QComboBox *kind;
    QLineEdit *amountEdit;
    QLineEdit *reasonEdit;
    Money value = 0;
};

/// The count didn't match. Blind: it never says by how much.
enum class MismatchChoice { Recount, Supervisor, Leave };
MismatchChoice askAfterMismatch(QWidget *parent);

/// What the supervisor sees, and only after authorizing: the figures, and the
/// reason the difference is accepted.
class DifferenceDialog : public QDialog
{
    Q_OBJECT
public:
    DifferenceDialog(Money expected, Money counted, QWidget *parent);
    QString reason() const;

private:
    QLineEdit *reasonEdit;
};

/// Past sessions, to reprint their closing sheet.
class SessionsDialog : public QDialog
{
    Q_OBJECT
public:
    SessionsDialog(CashService *cash, QWidget *parent);

private:
    void printSelected();

    CashService *cash;
    QTableWidget *table;
    QList<CashSession> list;
};

/// Print preview of a session's closing sheet.
void previewClosingSheet(const CashSessionReport &report, QWidget *parent);

#endif // CASHDIALOGS_H
