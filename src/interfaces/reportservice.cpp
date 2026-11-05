#include "reportservice.h"
#include "money.h"
#include "quantity.h"

#include <QDateTime>
#include <QLocale>
#include <QStringList>

namespace AguaraReport {

namespace {

/// 3,5 / 2 (a unit of something weighed, a whole one of what's counted).
QString quantityText(Quantity value)
{
    return AguaraQuantity::format(value, value % AguaraQuantity::One == 0 ? 0 : 3);
}

/// With its separator of thousands on the screen, plain for a spreadsheet.
QString csvCell(ReportCell kind, const QVariant &value)
{
    if (value.isNull())
        return QString();
    switch (kind) {
    case ReportCell::Money:
    case ReportCell::Integer:
        return QString::number(value.toLongLong());
    case ReportCell::Text: {
        const QString text = value.toString();
        return !text.isEmpty() && QStringLiteral("=+-@").contains(text.at(0)) ? QLatin1Char('\'') + text : text;
    }
    case ReportCell::Percent:
        return QString::number(value.toDouble(), 'f', 1).replace(QLatin1Char('.'), QLatin1Char(','));
    default:
        return formatCell(kind, value);
    }
}

QString quoted(QString text)
{
    if (text.contains(QLatin1Char(';')) || text.contains(QLatin1Char('"')) || text.contains(QLatin1Char('\n'))) {
        text.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        return QLatin1Char('"') + text + QLatin1Char('"');
    }
    return text;
}

} // namespace

QString formatCell(ReportCell kind, const QVariant &value)
{
    if (value.isNull())
        return QString();
    if (value.type() == QVariant::String)   // "Total", in a column of dates
        return value.toString();
    switch (kind) {
    case ReportCell::Money:
        return AguaraMoney::format(value.toLongLong());
    case ReportCell::Quantity:
        return quantityText(value.toLongLong());
    case ReportCell::Integer:
        return QString::number(value.toLongLong());
    case ReportCell::Percent:
        return QStringLiteral("%1 %").arg(QLocale(QLocale::Spanish, QLocale::Paraguay).toString(value.toDouble(), 'f', 1));
    case ReportCell::Date:
        return value.toDate().toString(QStringLiteral("dd/MM/yyyy"));
    case ReportCell::DateTime:
        return value.toDateTime().toString(QStringLiteral("dd/MM/yyyy HH:mm"));
    default:
        return value.toString();
    }
}

QByteArray toCsv(const ReportResult &report)
{
    const auto line = [&report](const QVector<QVariant> &row) {
        QStringList cells;
        for (int i = 0; i < report.columns.size(); ++i)
            cells << quoted(csvCell(report.columns.at(i).kind, i < row.size() ? row.at(i) : QVariant()));
        return cells.join(QLatin1Char(';'));
    };
    QStringList header;
    for (const ReportColumn &column : report.columns)
        header << quoted(column.title);
    QStringList lines = { header.join(QLatin1Char(';')) };
    for (const QVector<QVariant> &row : report.rows)
        lines << line(row);
    if (!report.totals.isEmpty())
        lines << line(report.totals);
    return QByteArray("\xEF\xBB\xBF") + lines.join(QStringLiteral("\r\n")).toUtf8() + "\r\n";
}

QString toHtml(const ReportResult &report)
{
    const auto cell = [](const char *tag, const ReportColumn &column, const QString &text) {
        const bool right = column.kind != ReportCell::Text && column.kind != ReportCell::Date && column.kind != ReportCell::DateTime;
        return QStringLiteral("<%1 align=\"%2\">%3</%1>").arg(QLatin1String(tag), right ? QStringLiteral("right") : QStringLiteral("left"),
                                                              text.toHtmlEscaped());
    };
    const auto rowOf = [&](const QVector<QVariant> &row, const char *tag) {
        QString cells;
        for (int i = 0; i < report.columns.size(); ++i) {
            const ReportColumn &column = report.columns.at(i);
            cells += cell(tag, column, i < row.size() ? formatCell(column.kind, row.at(i)) : QString());
        }
        return QStringLiteral("<tr>%1</tr>").arg(cells);
    };

    QString head;
    for (const ReportColumn &column : report.columns)
        head += cell("th", column, column.title);
    QString rows;
    for (const QVector<QVariant> &row : report.rows)
        rows += rowOf(row, "td");
    if (!report.totals.isEmpty())
        rows += rowOf(report.totals, "th");

    return QStringLiteral("<html><body style=\"font-size: 9pt;\"><h2>%1</h2><p>%2</p>"
                          "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"3\" border=\"1\" style=\"border-collapse: collapse;\">"
                          "<tr>%3</tr>%4</table><p>%5</p></body></html>")
        .arg(report.title.toHtmlEscaped(), report.subtitle.toHtmlEscaped(), head, rows, report.note.toHtmlEscaped());
}

} // namespace AguaraReport
