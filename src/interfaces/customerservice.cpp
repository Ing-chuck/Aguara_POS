#include "customerservice.h"

#include <QRegularExpression>

namespace AguaraCustomer {

int rucCheckDigit(const QString &base)
{
    int total = 0;
    int weight = 2;
    for (int i = base.size() - 1; i >= 0; --i) {
        const QChar c = base.at(i).toUpper();
        total += (c.isDigit() ? c.digitValue() : c.unicode()) * weight;
        weight = weight == 11 ? 2 : weight + 1;
    }
    const int remainder = total % 11;
    return remainder > 1 ? 11 - remainder : 0;
}

bool validRuc(const QString &ruc)
{
    static const QRegularExpression shape(QStringLiteral("^([0-9A-Za-z]{1,12})-([0-9])$"));
    const QRegularExpressionMatch match = shape.match(ruc);
    return match.hasMatch() && match.captured(2).toInt() == rucCheckDigit(match.captured(1));
}

bool validCi(const QString &ci)
{
    static const QRegularExpression shape(QStringLiteral("^[0-9]{1,10}$"));
    return shape.match(ci).hasMatch();
}

} // namespace AguaraCustomer
