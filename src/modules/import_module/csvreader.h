#ifndef CSVREADER_H
#define CSVREADER_H

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

/// CSV reading (RFC 4180): fields may be quoted with "..."; inside quotes the
/// separator and line breaks are literal and "" is a quote character.
namespace Csv {

/// One row, with the file line it starts on (for error messages).
struct Record {
    int         line = 0;
    QStringList fields;
};

/// Decode file contents: UTF-8 (with or without BOM) when valid, otherwise
/// Windows-1252, which is what Excel writes on Spanish Windows.
QString decode(const QByteArray &bytes);

/// ';' if the first row has more semicolons than commas (outside quotes), else ','.
QChar detectSeparator(const QString &text);

/// Split text into records. Accepts CRLF, LF or CR line ends. Lenient with
/// malformed input: a quote inside an unquoted field is kept as a character,
/// and an unterminated quoted field runs to the end of the text.
QList<Record> parse(const QString &text, QChar separator);

} // namespace Csv

#endif // CSVREADER_H
