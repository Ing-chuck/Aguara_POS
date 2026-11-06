#ifndef ACTIVITYLOG_H
#define ACTIVITYLOG_H

#include <QString>

/*
    What the program did, and when, in logs\actividad-AAAA-MM.log. It stays on
    this computer and is always on, whatever Preferencias > Telemetría says.

        2026-11-06 14:03:12 venta.cobrar
        2026-11-06 14:03:40 reporte.iva.libro

    It answers "what was going on just before this failed", next to aguara.log
    (the technical errors) and logs\crashes (what the program left when it died).
    Who did what, with its details, is the audit log, in the database: this file
    has only the names of events, because folders like this one get copied and
    sent around when someone asks for help.

    A month a file, and the last twelve are kept: a busy till writes some
    megabytes a year.
*/
namespace ActivityLog {

constexpr int KeptMonths = 12;

/// False if the folder can't be written: the program goes on without it.
bool open(const QString &directory);
void close();

/// A line, right away: what comes just before a crash is what matters most.
void record(const QString &event);

/// The last run didn't reach close(): a crash, a forced close or a power cut.
bool lastRunEndedBadly();

} // namespace ActivityLog

#endif // ACTIVITYLOG_H
