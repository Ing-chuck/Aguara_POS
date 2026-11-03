#ifndef MONEY_H
#define MONEY_H

#include "aguara_global.h"

#include <QString>

/*
    Money in Aguara
    ---------------
    All amounts are whole guaraníes (PYG) stored as 64-bit integers.

    * ISO 4217 defines PYG with 0 minor units: there are no céntimos, so
      there is no fractional part to keep. Integers are exact; floating point
      (the old DOUBLE columns) is not.
    * Prices are stored IVA included, as they are written on shelves and on
      Paraguayan invoices (SIFEN works with IVA-included whole guaraníes).
      The IVA part of a price is derived with vatIncluded().
    * Rounding, where a fraction appears (parsing "1.500,50", IVA), is to the
      nearest guaraní, halves away from zero.
*/
using Money = qint64;

namespace AguaraMoney {

/// "15.000" (es_PY thousands separator, no decimals).
AGUARA_EXPORT QString format(Money amount);

/// "Gs. 15.000"
AGUARA_EXPORT QString formatWithSymbol(Money amount);

/// Parse user or file input. Accepts "15000", "15.000", "15,000",
/// "Gs. 15.000" and a trailing 1-2 digit fraction ("15000.50", "15.000,50"),
/// which is rounded to the nearest guaraní. A separator followed by exactly
/// three digits is always a thousands separator.
/// Returns false (and leaves *amount untouched) if the text is not an amount.
AGUARA_EXPORT bool parse(const QString &text, Money *amount);

/// IVA contained in an IVA-included price: round(gross * rate / (100 + rate)).
/// e.g. vatIncluded(11000, 10) == 1000, vatIncluded(5000, 5) == 238.
AGUARA_EXPORT Money vatIncluded(Money grossPrice, int ratePercent);

} // namespace AguaraMoney

#endif // MONEY_H
