#ifndef CLOSINGSHEET_H
#define CLOSINGSHEET_H

#include <cashservice.h>

#include <QString>

class QPrinter;

/// The closing sheet of a cash session: by payment method, movements,
/// count and differences.
namespace ClosingSheet {

QString html(const CashSessionReport &report);
void print(QPrinter *printer, const CashSessionReport &report);

} // namespace ClosingSheet

#endif // CLOSINGSHEET_H
