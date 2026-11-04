#ifndef LABELSHEET_H
#define LABELSHEET_H

#include <productservice.h>

class QPrinter;

/// Shelf labels on A4 paper: 3 columns by 7 rows, with description, price
/// and, for valid EAN-13 codes, the bars.
namespace LabelSheet {

constexpr int Columns = 3;
constexpr int Rows = 7;

/// Print the labels, one per entry in `products`. Returns the pages used.
int print(QPrinter *printer, const QList<Product> &products);

} // namespace LabelSheet

#endif // LABELSHEET_H
