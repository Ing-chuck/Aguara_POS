#include "quantity.h"
#include "money.h"

#include <QRegularExpression>

namespace {
/// 10^n for n in 0..3
int power10(int n)
{
    int value = 1;
    while (n-- > 0)
        value *= 10;
    return value;
}
} // namespace

namespace AguaraQuantity {

QString format(Quantity quantity, int decimals)
{
    const bool negative = quantity < 0;
    const qint64 absolute = negative ? -quantity : quantity;
    QString text = AguaraMoney::format(absolute / One);

    int fraction = int(absolute % One);
    int digits = qBound(0, decimals, 3);
    while (digits < 3 && fraction % power10(3 - digits) != 0)
        ++digits;                       // more precision than the unit: show it
    if (digits > 0) {
        fraction /= power10(3 - digits);
        text += QLatin1Char(',') + QString::number(fraction).rightJustified(digits, QLatin1Char('0'));
    }
    return negative ? QLatin1Char('-') + text : text;
}

bool parse(const QString &input, Quantity *quantity)
{
    QString text = input.trimmed();
    const bool negative = text.startsWith(QLatin1Char('-'));
    if (negative)
        text.remove(0, 1);
    static const QRegularExpression shape(QStringLiteral("^\\d[\\d.,]*$"));
    if (!shape.match(text).hasMatch() || text.count(QLatin1Char(',')) > 1)
        return false;

    QString digits = text;
    QString fraction;
    const int comma = text.indexOf(QLatin1Char(','));
    if (comma >= 0) {
        digits = text.left(comma);
        fraction = text.mid(comma + 1);
        static const QRegularExpression grouped(QStringLiteral("^\\d{1,3}(\\.\\d{3})*$|^\\d+$"));
        if (!grouped.match(digits).hasMatch())
            return false;
        digits.remove(QLatin1Char('.'));
    } else if (text.contains(QLatin1Char('.'))) {
        static const QRegularExpression thousands(QStringLiteral("^\\d{1,3}(\\.\\d{3})+$"));
        if (thousands.match(text).hasMatch()) {
            digits.remove(QLatin1Char('.'));
        } else {
            if (text.count(QLatin1Char('.')) > 1)
                return false;
            const int dot = text.indexOf(QLatin1Char('.'));
            digits = text.left(dot);
            fraction = text.mid(dot + 1);
        }
    }
    if (fraction.size() > 3 || fraction.contains(QLatin1Char('.')) || digits.isEmpty())
        return false;

    bool ok = false;
    const qint64 units = digits.toLongLong(&ok);
    if (!ok)
        return false;
    const qint64 thousandths = fraction.leftJustified(3, QLatin1Char('0')).toLongLong();
    *quantity = (units * One + thousandths) * (negative ? -1 : 1);
    return true;
}

qint64 lineTotal(qint64 unitAmount, Quantity quantity)
{
    const qint64 product = unitAmount * quantity;
    return (product >= 0 ? product + One / 2 : product - One / 2) / One;
}

} // namespace AguaraQuantity
