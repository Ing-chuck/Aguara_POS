#include "barcode.h"

namespace {

bool allDigits(const QString &text)
{
    for (const QChar c : text) {
        if (c < QLatin1Char('0') || c > QLatin1Char('9'))
            return false;
    }
    return !text.isEmpty();
}

// The three digit encodings of EAN-13: L and G on the left, R on the right.
const char *const kL[] = { "0001101", "0011001", "0010011", "0111101", "0100011",
                           "0110001", "0101111", "0111011", "0110111", "0001011" };
const char *const kG[] = { "0100111", "0110011", "0011011", "0100001", "0011101",
                           "0111001", "0000101", "0010001", "0001001", "0010111" };
const char *const kR[] = { "1110010", "1100110", "1101100", "1000010", "1011100",
                           "1001110", "1010000", "1000100", "1001000", "1110100" };
// The first digit isn't drawn: it decides which left digits use L or G.
const char *const kParity[] = { "LLLLLL", "LLGLGG", "LLGGLG", "LLGGGL", "LGLLGG",
                                "LGGLLG", "LGGGLL", "LGLGLG", "LGLGGL", "LGGLGL" };

} // namespace

namespace AguaraBarcode {

int ean13CheckDigit(const QString &first12)
{
    if (first12.size() != 12 || !allDigits(first12))
        return -1;
    int sum = 0;
    for (int i = 0; i < 12; ++i)
        sum += first12.at(i).digitValue() * (i % 2 ? 3 : 1);
    return (10 - sum % 10) % 10;
}

bool isValidEan13(const QString &code)
{
    return code.size() == 13 && allDigits(code)
        && ean13CheckDigit(code.left(12)) == code.at(12).digitValue();
}

QString ean13Bars(const QString &code)
{
    if (!isValidEan13(code))
        return QString();
    const char *parity = kParity[code.at(0).digitValue()];
    QString bars = QStringLiteral("101");
    for (int i = 1; i <= 6; ++i) {
        const int digit = code.at(i).digitValue();
        bars += QLatin1String(parity[i - 1] == 'L' ? kL[digit] : kG[digit]);
    }
    bars += QStringLiteral("01010");
    for (int i = 7; i <= 12; ++i)
        bars += QLatin1String(kR[code.at(i).digitValue()]);
    bars += QStringLiteral("101");
    return bars;
}

ScaleCode parseScaleCode(const QString &code)
{
    ScaleCode result;
    if (!isValidEan13(code) || code.at(0) != QLatin1Char('2'))
        return result;
    result.valid = true;
    result.articleCode = code.left(7);
    const qint64 value = code.mid(7, 5).toLongLong();
    if (code.at(1).digitValue() <= 7)
        result.grams = value;
    else
        result.price = value;
    return result;
}

} // namespace AguaraBarcode
