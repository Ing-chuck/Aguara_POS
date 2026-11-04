#ifndef QUANTITY_H
#define QUANTITY_H

#include "aguara_global.h"

#include <QString>

/*
    Quantities in Aguara
    --------------------
    Stock is counted in units or by weight or volume (2,750 kg), so a quantity
    has decimals. Like Money, it is an integer so that sums are exact: a
    Quantity is a number of thousandths.

        1 unit = 1000     2,750 kg = 2750

    Use AguaraQuantity::whole() and format(); never divide by hand.
*/
using Quantity = qint64;

namespace AguaraQuantity {

constexpr Quantity One = 1000;

constexpr Quantity whole(qint64 units) { return units * One; }

/// "24", "2,750". `decimals` is the unit's (0 for units, 3 for kg). A value
/// with more precision than that still shows it: nothing is hidden.
AGUARA_EXPORT QString format(Quantity quantity, int decimals = 0);

/// Parse user or file input: "24", "2,75", "2.75", "1.500" (1500), "1.500,5".
/// A comma is always the decimal mark. A dot followed by exactly three digits
/// separates thousands; any other dot is the decimal mark (numeric keypads).
/// At most three decimals. Returns false if the text is not a quantity.
AGUARA_EXPORT bool parse(const QString &text, Quantity *quantity);

/// amount * quantity, rounded to the guaraní: a line of a purchase or sale.
AGUARA_EXPORT qint64 lineTotal(qint64 unitAmount, Quantity quantity);

} // namespace AguaraQuantity

#endif // QUANTITY_H
