#ifndef REPORTPAINTER_H
#define REPORTPAINTER_H

#include <reportservice.h>

class QPrinter;

namespace ReportPainter {

/// Draws a report on paper, or into a PDF, one page at a time: the title, the
/// columns again on every page, long text in several lines, the totals, the
/// note and the page numbers.
///
/// It doesn't go through HTML: QTextDocument lays out a table all at once, which
/// took 12 s for 2.700 rows and ran a 32-bit program out of memory at 177.000.
/// Here the time grows with the rows, and memory stays about the same.
///
/// False if the printer can't be drawn on, a PDF that is open elsewhere for one.
bool paint(const ReportResult &report, QPrinter *printer);

} // namespace ReportPainter

#endif // REPORTPAINTER_H
