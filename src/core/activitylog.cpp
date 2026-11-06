#include "activitylog.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>

namespace {

const char *const kRunningMarker = "programa-en-uso";

struct State {
    QMutex mutex;
    QString directory;
    QString month;
    QFile file;
    bool endedBadly = false;
};

State &state()
{
    static State s;
    return s;
}

/// The names sort by date, so the oldest come first.
void forgetOldMonths(const QString &directory)
{
    const QStringList files = QDir(directory).entryList({ QStringLiteral("actividad-*.log") }, QDir::Files, QDir::Name);
    for (int i = 0; i < files.size() - ActivityLog::KeptMonths; ++i)
        QFile::remove(QDir(directory).filePath(files.at(i)));
}

void useMonth(State &s, const QString &month)
{
    if (s.month == month && s.file.isOpen())
        return;
    s.file.close();
    s.month = month;
    s.file.setFileName(QDir(s.directory).filePath(QStringLiteral("actividad-%1.log").arg(month)));
    if (s.file.open(QIODevice::Append | QIODevice::Text))
        forgetOldMonths(s.directory);
}

} // namespace

namespace ActivityLog {

bool open(const QString &directory)
{
    State &s = state();
    QMutexLocker lock(&s.mutex);
    if (!QDir().mkpath(directory))
        return false;
    s.directory = directory;
    QFile marker(QDir(directory).filePath(QLatin1String(kRunningMarker)));
    s.endedBadly = marker.exists();
    if (marker.open(QIODevice::WriteOnly))
        marker.close();
    useMonth(s, QDate::currentDate().toString(QStringLiteral("yyyy-MM")));
    return s.file.isOpen();
}

void close()
{
    State &s = state();
    QMutexLocker lock(&s.mutex);
    if (s.directory.isEmpty())
        return;
    s.file.close();
    QFile::remove(QDir(s.directory).filePath(QLatin1String(kRunningMarker)));
    s.directory.clear();
    s.month.clear();
}

void record(const QString &event)
{
    State &s = state();
    QMutexLocker lock(&s.mutex);
    if (s.directory.isEmpty())
        return;
    const QDateTime now = QDateTime::currentDateTime();
    useMonth(s, now.toString(QStringLiteral("yyyy-MM")));
    if (!s.file.isOpen())
        return;
    s.file.write((now.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss ")) + event + QLatin1Char('\n')).toUtf8());
    s.file.flush();
}

bool lastRunEndedBadly()
{
    State &s = state();
    QMutexLocker lock(&s.mutex);
    return s.endedBadly;
}

} // namespace ActivityLog
