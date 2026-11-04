#ifndef BARCODE_H
#define BARCODE_H

#include "aguara_global.h"

#include <QString>

/// EAN-13: the barcode on retail packaging, and the one scales print.
namespace AguaraBarcode {

/// The check digit of the first 12 digits, or -1 if they aren't 12 digits.
AGUARA_EXPORT int ean13CheckDigit(const QString &first12);

/// 13 digits with a correct check digit.
AGUARA_EXPORT bool isValidEan13(const QString &code);

/// The bars of a valid code: 95 characters, '1' a bar and '0' a space.
/// Empty if the code isn't valid.
AGUARA_EXPORT QString ean13Bars(const QString &code);

/// A code printed by a scale. Prefix 20-29, then a 5-digit article number and
/// a 5-digit value:
///   prefixes 20-27: the weight in grams
///   prefixes 28-29: the price in guaraníes
/// The article is found by the first 7 digits (prefix + article number), so
/// a weighed article's code is, for example, 2012345.
struct ScaleCode {
    bool    valid = false;
    QString articleCode;    ///< first 7 digits
    qint64  grams = 0;      ///< 0 if the code carries a price
    qint64  price = 0;      ///< 0 if the code carries a weight
};
AGUARA_EXPORT ScaleCode parseScaleCode(const QString &code);

} // namespace AguaraBarcode

#endif // BARCODE_H
