#include "money.h"

#include <QLocale>
#include <QRegularExpression>

namespace AguaraMoney {

QString format(Money amount)
{
    static const QLocale paraguay(QLocale::Spanish, QLocale::Paraguay);
    return paraguay.toString(amount);
}

QString formatWithSymbol(Money amount)
{
    return QStringLiteral("Gs. ") + format(amount);
}

bool parse(const QString &text, Money *amount)
{
    QString s = text.trimmed();
    s.remove(QRegularExpression(QStringLiteral("^(gs\\.?|₲)\\s*"),
                                QRegularExpression::CaseInsensitiveOption));
    s.remove(QLatin1Char(' '));

    bool negative = s.startsWith(QLatin1Char('-'));
    if (negative)
        s.remove(0, 1);

    // integer part: plain digits, or digits grouped in threes by '.' or ','
    // optional fraction: '.' or ',' followed by 1 or 2 digits
    static const QRegularExpression re(
        QStringLiteral("^(\\d{1,3}(?:[.,]\\d{3})+|\\d+)(?:[.,](\\d{1,2}))?$"));
    const QRegularExpressionMatch m = re.match(s);
    if (!m.hasMatch())
        return false;

    QString integerPart = m.captured(1);
    integerPart.remove(QLatin1Char('.'));
    integerPart.remove(QLatin1Char(','));
    if (integerPart.size() > 15)        // far beyond any real amount; avoids overflow
        return false;

    Money value = integerPart.toLongLong();
    const QString fraction = m.captured(2);
    if (!fraction.isEmpty()) {
        const int hundredths = fraction.size() == 1 ? fraction.toInt() * 10 : fraction.toInt();
        if (hundredths >= 50)
            ++value;
    }

    *amount = negative ? -value : value;
    return true;
}

Money vatIncluded(Money grossPrice, int ratePercent)
{
    if (ratePercent <= 0)
        return 0;
    const Money numerator = grossPrice * ratePercent;
    const Money denominator = 100 + ratePercent;
    const Money half = denominator / 2;
    // round half away from zero
    return numerator >= 0 ? (numerator + half) / denominator
                          : -((-numerator + half) / denominator);
}

} // namespace AguaraMoney
