#ifndef ESCPOS_H
#define ESCPOS_H

#include <printservice.h>

#include <QByteArray>

/// The bytes a thermal printer understands. Nothing here touches a printer.
namespace EscPos {

/// Text in code page 850, the one with ñ and accents that Epson, Bixolon and
/// the generic 80 mm printers all have. What it lacks becomes "?". Control
/// characters go away: in a description they would be printer commands.
QByteArray encode(const QString &text);

/// `text` in lines of at most `width` characters, breaking at spaces.
QStringList wrap(const QString &text, int width);

/// The pulse that opens a drawer wired to the printer's RJ-11 port.
QByteArray drawerPulse();

/// The whole job. With `openDrawer` the pulse goes first, so the drawer doesn't
/// wait for the paper.
QByteArray render(const Receipt &receipt, int columns, bool cut, bool openDrawer);

} // namespace EscPos

#endif // ESCPOS_H
