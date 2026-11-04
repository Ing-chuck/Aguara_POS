#include "returnservice.h"

namespace AguaraReturn {

Money amount(const ReturnableSale &sale, const QList<ReturnLine> &lines)
{
    Money total = 0;
    for (const ReturnLine &line : lines) {
        for (const ReturnableItem &item : sale.items) {
            if (item.line != line.line || line.quantity <= 0)
                continue;
            if (item.returned + line.quantity >= item.sold)
                total += item.net - item.refunded;      // the rest: the parts add up exactly
            else
                total += (item.net * line.quantity + item.sold / 2) / item.sold;
        }
    }
    return total;
}

Money paid(const QString &method, Money amount)
{
    return method == QLatin1String(PaymentMethod::Cash) ? amount + AguaraSale::cashRounding(amount) : amount;
}

QString formatNumber(qint64 id)
{
    return QStringLiteral("D-%1").arg(id, 6, 10, QLatin1Char('0'));
}

} // namespace AguaraReturn
