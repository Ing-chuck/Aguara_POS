#include "csvreader.h"

#include <QTextCodec>

namespace Csv {

QString decode(const QByteArray &bytes)
{
    QByteArray data = bytes;
    if (data.startsWith("\xEF\xBB\xBF"))
        data.remove(0, 3);

    QTextCodec *utf8 = QTextCodec::codecForName("UTF-8");
    QTextCodec::ConverterState state;
    const QString text = utf8->toUnicode(data.constData(), data.size(), &state);
    if (state.invalidChars == 0)
        return text;

    QTextCodec *ansi = QTextCodec::codecForName("Windows-1252");
    return ansi ? ansi->toUnicode(data) : text;
}

QChar detectSeparator(const QString &text)
{
    int semicolons = 0, commas = 0;
    bool quoted = false;
    for (const QChar c : text) {
        if (c == QLatin1Char('"'))
            quoted = !quoted;
        else if (!quoted && (c == QLatin1Char('\n') || c == QLatin1Char('\r'))) {
            if (semicolons + commas > 0)
                break;          // else a title above the table: the next row says
        }
        else if (!quoted && c == QLatin1Char(';'))
            ++semicolons;
        else if (!quoted && c == QLatin1Char(','))
            ++commas;
    }
    return semicolons > commas ? QLatin1Char(';') : QLatin1Char(',');
}

QList<Record> parse(const QString &text, QChar separator)
{
    QList<Record> records;
    Record record;
    QString field;
    bool inQuotes = false;
    bool fieldStarted = false;  // anything read for the current field (even "")
    int line = 1;
    record.line = line;

    auto endField = [&]() {
        record.fields.append(field);
        field.clear();
        fieldStarted = false;
    };
    auto endRecord = [&]() {
        endField();
        records.append(record);
        record = Record();
        record.line = line;
    };

    const int n = text.size();
    for (int i = 0; i < n; ++i) {
        const QChar c = text.at(i);

        if (inQuotes) {
            if (c == QLatin1Char('"')) {
                if (i + 1 < n && text.at(i + 1) == QLatin1Char('"')) {
                    field += QLatin1Char('"');      // "" -> "
                    ++i;
                } else {
                    inQuotes = false;               // closing quote
                }
            } else {
                if (c == QLatin1Char('\n'))
                    ++line;
                field += c;                         // separators/newlines are literal
            }
            continue;
        }

        if (c == QLatin1Char('"') && !fieldStarted) {
            inQuotes = true;
            fieldStarted = true;
        } else if (c == separator) {
            endField();
        } else if (c == QLatin1Char('\r') || c == QLatin1Char('\n')) {
            if (c == QLatin1Char('\r') && i + 1 < n && text.at(i + 1) == QLatin1Char('\n'))
                ++i;                                // CRLF
            ++line;
            endRecord();
        } else {
            field += c;
            fieldStarted = true;
        }
    }

    // last record, unless the text ended with a line break
    if (fieldStarted || !field.isEmpty() || !record.fields.isEmpty())
        endRecord();
    return records;
}

} // namespace Csv
