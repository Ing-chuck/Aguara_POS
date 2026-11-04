#include "escpos.h"

#include <QTextCodec>

namespace EscPos {

namespace {

const char Esc = 0x1B;
const char Gs = 0x1D;

QByteArray command(std::initializer_list<int> bytes)
{
    QByteArray result;
    for (int b : bytes)
        result.append(char(b));
    return result;
}

QByteArray initialize()
{
    return command({ Esc, '@' })            // back to defaults
         + command({ Esc, 't', 2 });        // code page 850
}

QByteArray align(bool center)
{
    return command({ Esc, 'a', center ? 1 : 0 });
}

QByteArray bold(bool on)
{
    return command({ Esc, 'E', on ? 1 : 0 });
}

/// Characters twice as wide and/or tall.
QByteArray size(bool wide, bool tall)
{
    return command({ Gs, '!', (wide ? 0x10 : 0) | (tall ? 0x01 : 0) });
}

/// `left` and `right` on one line, or `left` above if they don't fit.
QStringList fit(const QString &left, const QString &right, int width)
{
    if (right.isEmpty())
        return wrap(left, width);
    if (left.size() + 1 + right.size() <= width)
        return { left + QString(width - left.size() - right.size(), QLatin1Char(' ')) + right };
    QStringList lines = wrap(left, width);
    lines << right.rightJustified(width);
    return lines;
}

} // namespace

QByteArray encode(const QString &text)
{
    QString clean;
    clean.reserve(text.size());
    for (const QChar c : text) {
        if (c.unicode() >= 0x20 && c.unicode() != 0x7F)
            clean += c;
    }
    static QTextCodec *const codec = QTextCodec::codecForName("IBM850");
    Q_ASSERT(codec);
    QTextCodec::ConverterState state(QTextCodec::ConvertInvalidToNull);
    QByteArray bytes = codec->fromUnicode(clean.constData(), clean.size(), &state);
    bytes.replace('\0', '?');
    return bytes;
}

QStringList wrap(const QString &text, int width)
{
    QStringList lines;
    QString line;
    for (QString word : text.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        while (word.size() > width) {           // a code longer than the paper
            if (!line.isEmpty()) {
                lines << line;
                line.clear();
            }
            lines << word.left(width);
            word.remove(0, width);
        }
        if (!line.isEmpty() && line.size() + 1 + word.size() > width) {
            lines << line;
            line.clear();
        }
        if (!line.isEmpty())
            line += QLatin1Char(' ');
        line += word;
    }
    if (!line.isEmpty() || lines.isEmpty())
        lines << line;
    return lines;
}

QByteArray drawerPulse()
{
    return command({ Esc, 'p', 0, 25, 250 });   // pin 2, on 50 ms, off 500 ms
}

QByteArray render(const Receipt &receipt, int columns, bool cut, bool openDrawer)
{
    QByteArray out = initialize();
    if (openDrawer)
        out += drawerPulse();

    for (const ReceiptLine &line : receipt.lines) {
        if (line.rule) {
            out += QByteArray(columns, '-') + '\n';
            continue;
        }
        const bool title = line.style == ReceiptLine::Title;
        const bool strong = title || line.style == ReceiptLine::Strong || line.style == ReceiptLine::Total;
        const bool tall = title || line.style == ReceiptLine::Total;
        const int width = title ? columns / 2 : columns;    // double width: half as many fit

        out += align(title || line.centered) + bold(strong) + size(title, tall);
        for (const QString &text : fit(line.left, line.right, width))
            out += encode(text) + '\n';
        out += size(false, false) + bold(false) + align(false);
    }

    out += command({ Esc, 'd', 4 });            // past the cutter
    if (cut)
        out += command({ Gs, 'V', 66, 0 });     // partial cut
    return out;
}

} // namespace EscPos
