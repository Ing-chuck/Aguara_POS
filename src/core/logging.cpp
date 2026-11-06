#include "logging.h"

#include "crashhandler.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QTextStream>

Q_LOGGING_CATEGORY(lcApp, "aguara.app")
Q_LOGGING_CATEGORY(lcDatabase, "aguara.db")
Q_LOGGING_CATEGORY(lcModules, "aguara.modules")

namespace {

struct LogState {
    QMutex  mutex;
    QFile   file;
    QString directory;
    qint64  maxBytes = 0;
    int     keep = 0;
    bool    debug = false;
    bool    installed = false;
    QtMessageHandler previous = nullptr;
};

LogState &state()
{
    static LogState s;
    return s;
}

QString logName(int index)
{
    return index == 0 ? QStringLiteral("aguara.log")
                      : QStringLiteral("aguara.%1.log").arg(index);
}

/// aguara.log -> aguara.1.log -> ...; the oldest is deleted. Caller holds the mutex.
void rotate(LogState &s)
{
    s.file.close();
    QDir dir(s.directory);
    dir.remove(logName(s.keep - 1));
    for (int i = s.keep - 2; i >= 0; --i)
        dir.rename(logName(i), logName(i + 1));
    s.file.open(QIODevice::Append | QIODevice::Text);
}

const char *levelName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:    return "DEBUG";
    case QtInfoMsg:     return "INFO ";
    case QtWarningMsg:  return "WARN ";
    case QtCriticalMsg: return "ERROR";
    case QtFatalMsg:    return "FATAL";
    }
    return "?    ";
}

void handler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    LogState &s = state();
    QtMessageHandler previous = nullptr;
    {
        QMutexLocker lock(&s.mutex);
        previous = s.previous;
        if (s.file.isOpen() && (type != QtDebugMsg || s.debug)) {
            if (s.keep > 1 && s.file.size() >= s.maxBytes)
                rotate(s);
            QTextStream out(&s.file);
            out.setCodec("UTF-8");
            out << QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))
                << ' ' << levelName(type)
                << " [" << (context.category ? context.category : "default") << "] "
                << message << '\n';
            out.flush();
        }
    }
    if (type == QtFatalMsg)
        CrashHandler::fatalMessage();         // Qt ends the process right after this
    if (previous)
        previous(type, context, message);     // debugger / console, and abort() on qFatal
}

} // namespace

namespace AguaraLog {

QString defaultDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/logs");
}

bool install(const QString &directory, bool debug, qint64 maxBytes, int keep)
{
    LogState &s = state();
    QMutexLocker lock(&s.mutex);
    if (s.installed)
        return true;
    if (!QDir().mkpath(directory))
        return false;
    s.directory = directory;
    s.maxBytes  = qMax<qint64>(maxBytes, 1024);
    s.keep      = qMax(keep, 1);
    s.debug     = debug;
    s.file.setFileName(QDir(directory).filePath(logName(0)));
    if (!s.file.open(QIODevice::Append | QIODevice::Text))
        return false;
    s.previous  = qInstallMessageHandler(handler);
    s.installed = true;
    return true;
}

void uninstall()
{
    LogState &s = state();
    QMutexLocker lock(&s.mutex);
    if (!s.installed)
        return;
    qInstallMessageHandler(s.previous);
    s.previous = nullptr;
    s.file.close();
    s.installed = false;
}

QString filePath()
{
    LogState &s = state();
    QMutexLocker lock(&s.mutex);
    return s.installed ? s.file.fileName() : QString();
}

} // namespace AguaraLog
