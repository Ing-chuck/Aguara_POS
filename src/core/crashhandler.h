#ifndef CRASHHANDLER_H
#define CRASHHANDLER_H

#include <QString>

/// Leaves a crash-<id>.json in `directory` when the program dies: the error,
/// where it happened, the version and the time, and the PE time stamp of each
/// module, which says which build it was: the telemetry server finds its symbols
/// with that. No memory contents, no logs, no paths. Always on: the file stays on this computer, and only Telemetry,
/// if an administrator turned it on, sends it anywhere.
///
/// A crashed process can't be trusted to allocate memory or take locks, so the
/// handler only uses what install() prepared and plain Windows calls. It works
/// out the places by scanning the stack for return addresses, because release
/// builds keep no frame pointers: some entries may be leftovers, and the first
/// ones are what counts.
namespace CrashHandler {

void install(const QString &directory, const QString &version);
/// For the log's handler: Qt ends a qFatal without going through abort(). The
/// message itself stays out of the report: it could quote real data.
void fatalMessage();

} // namespace CrashHandler

#endif // CRASHHANDLER_H
