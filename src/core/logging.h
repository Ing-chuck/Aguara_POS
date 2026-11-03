#ifndef LOGGING_H
#define LOGGING_H

#include <QLoggingCategory>
#include <QString>

// Categories, so the log says where a message comes from and noisy areas can
// be filtered (QT_LOGGING_RULES="aguara.db.debug=true").
Q_DECLARE_LOGGING_CATEGORY(lcApp)       // "aguara.app": startup, settings, backup
Q_DECLARE_LOGGING_CATEGORY(lcDatabase)  // "aguara.db": SQL errors, migrations, connection
Q_DECLARE_LOGGING_CATEGORY(lcModules)   // "aguara.modules": plugin loading

/*
    The log file. Technical details (SQL errors, driver messages, module
    problems) go here, while the user sees a short message in Spanish
    (see errors.h).

    <dir>/aguara.log, rotated when it passes maxBytes:
    aguara.log -> aguara.1.log -> ... -> aguara.<keep-1>.log (oldest deleted).
    Thread-safe (the connection monitor logs from its own thread).
*/
namespace AguaraLog {

/// %APPDATA%\Aguara\AguaraPOS\logs
QString defaultDirectory();

/// Send every qDebug/qInfo/qWarning/qCritical/qFatal to the log file, and
/// still to the previous handler (debugger / console). Debug messages are
/// written only when `debug` is true (debug builds, or AGUARA_DEBUG=1).
bool install(const QString &directory, bool debug, qint64 maxBytes = 1024 * 1024, int keep = 5);

/// Restore the previous handler and close the file.
void uninstall();

/// The current log file, or empty when not installed.
QString filePath();

} // namespace AguaraLog

#endif // LOGGING_H
