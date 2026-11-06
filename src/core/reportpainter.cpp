#include "reportpainter.h"

#include <QCoreApplication>
#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPrinter>
#include <QVector>

#include <algorithm>

namespace {

const int kBodyPoints = 8;
const int kTitlePoints = 14;
const int kMeasuredRows = 2000;     // enough to know how wide a column gets
const int kMostLines = 12;          // a very long text is cut there, not the whole page

QString text(const char *source)
{
    return QCoreApplication::translate("ReportPainter", source);
}

bool isWrapped(const ReportColumn &column)
{
    return column.kind == ReportCell::Text;
}

Qt::Alignment alignment(const ReportColumn &column)
{
    const bool left = column.kind == ReportCell::Text || column.kind == ReportCell::Date || column.kind == ReportCell::DateTime;
    return (left ? Qt::AlignLeft : Qt::AlignRight) | Qt::AlignTop;
}

QString cell(const ReportColumn &column, const QVector<QVariant> &row, int index)
{
    return index < row.size() ? AguaraReport::formatCell(column.kind, row.at(index)) : QString();
}

/// One report on one kind of paper: everything that doesn't change from page to page.
class Sheet
{
public:
    Sheet(const ReportResult &report, QPrinter *printer, QPainter *painter)
        : report(report), printer(printer), painter(painter),
          body(printer->pageRect(QPrinter::DevicePixel).translated(-printer->pageRect(QPrinter::DevicePixel).topLeft())),
          normal(QStringLiteral("Segoe UI"), kBodyPoints), bold(normal), title(normal),
          normalMetrics(normal, printer), boldMetrics(bold, printer)
    {
        bold.setBold(true);
        title.setBold(true);
        title.setPointSize(kTitlePoints);
        boldMetrics = QFontMetricsF(bold, printer);
        pad = normalMetrics.averageCharWidth() * 0.6;
        line = normalMetrics.lineSpacing();
        footer = line * 1.6;
        header = line + pad * 2;
        measureHeading();
        measureColumns();
    }

    bool draw()
    {
        // first the heights, because each page says how many there are
        heights.reserve(report.rows.size());
        for (const QVector<QVariant> &row : report.rows)
            heights << rowHeight(row, normalMetrics);
        const qreal totals = report.totals.isEmpty() ? 0 : rowHeight(report.totals, boldMetrics);
        const qreal note = noteHeight();
        pages = countPages(totals, note);

        y = headingHeight;
        pageNumber = 1;
        painter->setPen(Qt::black);
        drawHeading();
        drawColumnTitles();
        for (int row = 0; row < report.rows.size(); ++row) {
            if (!roomFor(heights.at(row)))
                return false;
            drawRow(report.rows.at(row), heights.at(row), normal, row % 2 == 1);
        }
        if (!report.totals.isEmpty()) {
            if (!roomFor(totals))
                return false;
            drawRow(report.totals, totals, bold, false);
        }
        if (note > 0)
            drawNote(note);
        drawFooter();
        return true;
    }

private:
    qreal bottom() const { return body.height() - footer; }

    void measureHeading()
    {
        const QFontMetricsF titleMetrics(title, printer);
        headingHeight = titleMetrics.lineSpacing() + line * 0.6;
        if (!report.subtitle.isEmpty())
            headingHeight += normalMetrics.boundingRect(QRectF(0, 0, body.width(), bottom()), Qt::TextWordWrap, report.subtitle).height() + line * 0.6;
    }

    /// As wide as the text asks for; the page's spare width, or what it lacks,
    /// goes to the columns of text: figures are never cut.
    void measureColumns()
    {
        const int count = report.columns.size();
        widths.fill(0, count);
        const int step = qMax(1, report.rows.size() / kMeasuredRows);
        for (int c = 0; c < count; ++c) {
            const ReportColumn &column = report.columns.at(c);
            qreal widest = boldMetrics.horizontalAdvance(column.title);
            for (int row = 0; row < report.rows.size(); row += step)
                widest = qMax(widest, normalMetrics.horizontalAdvance(cell(column, report.rows.at(row), c)));
            if (!report.totals.isEmpty())
                widest = qMax(widest, boldMetrics.horizontalAdvance(cell(column, report.totals, c)));
            widths[c] = widest + pad * 2.5;
        }

        qreal fixed = 0, flexible = 0;
        for (int c = 0; c < count; ++c)
            (isWrapped(report.columns.at(c)) ? flexible : fixed) += widths.at(c);
        const qreal page = body.width();
        if (flexible > 0 && fixed < page) {
            const qreal scale = (page - fixed) / flexible;
            for (int c = 0; c < count; ++c) {
                if (isWrapped(report.columns.at(c)))
                    widths[c] *= scale;
            }
        } else if (fixed + flexible > 0) {              // figures alone fill the page, or it has room for them
            const qreal scale = page / (fixed + flexible);
            for (qreal &width : widths)
                width *= scale;
        }
    }

    QRectF textArea(qreal x, int column, qreal top, qreal height) const
    {
        return QRectF(x + pad, top + pad, widths.at(column) - pad * 2, height - pad * 2);
    }

    /// Only text can take more than a line, and it's rarely wider than its column.
    qreal rowHeight(const QVector<QVariant> &row, const QFontMetricsF &metrics) const
    {
        qreal height = line;
        for (int c = 0; c < report.columns.size(); ++c) {
            const ReportColumn &column = report.columns.at(c);
            if (!isWrapped(column))
                continue;
            const qreal width = widths.at(c) - pad * 2;
            const QString value = cell(column, row, c);
            if (value.isEmpty() || metrics.horizontalAdvance(value) <= width)
                continue;
            height = qMax(height, metrics.boundingRect(QRectF(0, 0, width, line * (kMostLines + 1)), Qt::TextWordWrap, value).height());
        }
        return qMin(height, line * kMostLines) + pad * 2;
    }

    qreal noteHeight() const
    {
        if (report.note.isEmpty())
            return 0;
        return normalMetrics.boundingRect(QRectF(0, 0, body.width(), bottom()), Qt::TextWordWrap, report.note).height() + line;
    }

    int countPages(qreal totals, qreal note) const
    {
        int count = 1;
        qreal at = headingHeight + header;
        const auto place = [&](qreal height) {
            if (at + height > bottom()) {
                ++count;
                at = header;
            }
            at += height;
        };
        for (qreal height : heights)
            place(height);
        if (totals > 0)
            place(totals);
        if (note > 0)
            place(note);
        return count;
    }

    bool roomFor(qreal height)
    {
        if (y + height <= bottom())
            return true;
        drawFooter();
        if (!printer->newPage())
            return false;
        ++pageNumber;
        y = 0;
        drawColumnTitles();
        return true;
    }

    void drawHeading()
    {
        const QFontMetricsF titleMetrics(title, printer);
        painter->setFont(title);
        painter->drawText(QRectF(0, 0, body.width(), titleMetrics.lineSpacing()), Qt::AlignLeft | Qt::AlignTop,
                          titleMetrics.elidedText(report.title, Qt::ElideRight, body.width()));
        if (!report.subtitle.isEmpty()) {
            painter->setFont(normal);
            painter->drawText(QRectF(0, titleMetrics.lineSpacing() + line * 0.3, body.width(), headingHeight), Qt::TextWordWrap, report.subtitle);
        }
    }

    void drawColumnTitles()
    {
        painter->fillRect(QRectF(0, y, body.width(), header), QColor(0xE9, 0xEC, 0xEF));
        painter->setFont(bold);
        qreal x = 0;
        for (int c = 0; c < report.columns.size(); ++c) {
            const ReportColumn &column = report.columns.at(c);
            const QRectF area = textArea(x, c, y, header);
            painter->drawText(area, alignment(column), boldMetrics.elidedText(column.title, Qt::ElideRight, area.width()));
            x += widths.at(c);
        }
        y += header;
        painter->setFont(normal);
    }

    /// A single line, at its place: several times faster than QPainter finding
    /// the place in a rectangle, and a long report is millions of these.
    void drawLine(const QRectF &area, const ReportColumn &column, const QString &value, qreal advance, const QFontMetricsF &metrics)
    {
        const qreal x = alignment(column) & Qt::AlignRight ? area.right() - advance : area.left();
        painter->drawText(QPointF(x, area.top() + metrics.ascent()), value);
    }

    void drawRow(const QVector<QVariant> &row, qreal height, const QFont &font, bool shaded)
    {
        if (shaded)
            painter->fillRect(QRectF(0, y, body.width(), height), QColor(0xF6, 0xF7, 0xF8));
        painter->setFont(font);
        const QFontMetricsF &metrics = &font == &bold ? boldMetrics : normalMetrics;
        qreal x = 0;
        for (int c = 0; c < report.columns.size(); ++c) {
            const ReportColumn &column = report.columns.at(c);
            const QString value = cell(column, row, c);
            if (!value.isEmpty()) {
                const QRectF area = textArea(x, c, y, height);
                const qreal advance = metrics.horizontalAdvance(value);
                if (advance <= area.width())
                    drawLine(area, column, value, advance, metrics);
                else if (isWrapped(column) && height - pad * 2 > line * 1.5)
                    painter->drawText(area, alignment(column) | Qt::TextWordWrap, value);
                else
                    drawLine(area, column, metrics.elidedText(value, Qt::ElideRight, area.width()), 0, metrics);
            }
            x += widths.at(c);
        }
        y += height;
    }

    void drawNote(qreal height)
    {
        if (!roomFor(height))
            return;
        painter->setFont(normal);
        painter->drawText(QRectF(0, y + line * 0.6, body.width(), height), Qt::TextWordWrap, report.note);
        y += height;
    }

    void drawFooter()
    {
        painter->setFont(normal);
        const QRectF area(0, bottom() + line * 0.4, body.width(), line);
        painter->drawText(area, Qt::AlignLeft | Qt::AlignTop, normalMetrics.elidedText(report.title, Qt::ElideRight, body.width() * 0.7));
        painter->drawText(area, Qt::AlignRight | Qt::AlignTop, text("Página %1 de %2").arg(pageNumber).arg(pages));
    }

    const ReportResult &report;
    QPrinter *printer;
    QPainter *painter;
    QRectF body;
    QFont normal, bold, title;
    QFontMetricsF normalMetrics, boldMetrics;
    QVector<qreal> widths, heights;
    qreal pad = 0, line = 0, footer = 0, header = 0, headingHeight = 0, y = 0;
    int pageNumber = 1, pages = 1;
};

} // namespace

namespace ReportPainter {

bool paint(const ReportResult &report, QPrinter *printer)
{
    QPainter painter;
    if (!painter.begin(printer))
        return false;
    Sheet sheet(report, printer, &painter);
    const bool drawn = sheet.draw();
    return painter.end() && drawn;
}

} // namespace ReportPainter
