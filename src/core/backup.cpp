#include "backup.h"
#include "database.h"
#include "logging.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace {
/// aguara-yyyyMMdd-HHmmss.db (built from parts: "??-" in a literal is a trigraph)
QString pattern()
{
    return QStringLiteral("aguara-%1-%2.db").arg(QString(8, QLatin1Char('?')), QString(6, QLatin1Char('?')));
}
}

Backup::Backup(Database *database)
    : m_database(database)
{
}

QString Backup::defaultDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/backups");
}

QString Backup::fileNameFor(const QDateTime &when)
{
    return when.toString(QStringLiteral("'aguara-'yyyyMMdd-HHmmss'.db'"));
}

bool Backup::isAvailable() const
{
    return m_database && m_database->isOpen() && m_database->backend() == DatabaseSettings::SQLite;
}

bool Backup::backupTo(const QString &file)
{
    if (!m_database) {
        m_lastError = QCoreApplication::translate("Backup", "No hay base de datos abierta.");
        return false;
    }
    if (m_database->backupTo(file))
        return true;
    m_lastError = m_database->lastError();
    return false;
}

bool Backup::automatic(const QString &directory, int keep, qint64 minAgeSecs, QString *made)
{
    if (made)
        made->clear();
    if (!isAvailable())
        return true;                // nothing to do here (PostgreSQL: backed up on the server)

    QDir dir(directory);
    // the names sort by date: newest first
    QStringList existing = dir.entryList({ pattern() }, QDir::Files, QDir::Name | QDir::Reversed);
    const QDateTime now = QDateTime::currentDateTime();
    if (!existing.isEmpty()) {
        const QDateTime newest = QDateTime::fromString(existing.first().mid(7, 15), QStringLiteral("yyyyMMdd-HHmmss"));
        if (newest.isValid() && newest.secsTo(now) < minAgeSecs && newest <= now)
            return true;            // already have a recent copy
    }

    const QString file = dir.filePath(fileNameFor(now));
    if (!backupTo(file))
        return false;
    if (made)
        *made = file;

    existing.prepend(QFileInfo(file).fileName());
    existing.removeDuplicates();
    for (int i = qMax(keep, 1); i < existing.size(); ++i) {
        if (dir.remove(existing.at(i)))
            qCInfo(lcApp) << "Deleted old backup" << existing.at(i);
    }
    return true;
}
