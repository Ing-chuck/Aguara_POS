#ifndef BACKUP_H
#define BACKUP_H

#include <QDateTime>
#include <QString>

class Database;

/*
    Copies of the SQLite database (one till, data on this computer).

    automatic(): at most one copy per day, kept in a folder with the newest
    `keep` copies (aguara-yyyyMMdd-HHmmss.db). main() calls it at startup.
    backupTo(): a copy where the user chooses (Archivo > Copia de seguridad).

    With PostgreSQL the data is on the server and is backed up there
    (scripts/backup-postgresql.ps1); these do nothing and say so.
*/
class Backup
{
public:
    explicit Backup(Database *database);

    static QString defaultDirectory();      ///< %APPDATA%\Aguara\AguaraPOS\backups
    static QString fileNameFor(const QDateTime &when);

    bool isAvailable() const;               ///< false for PostgreSQL

    bool backupTo(const QString &file);

    /// Make a copy in `directory` unless the newest one there is younger
    /// than `minAge`, then delete all but the newest `keep` copies.
    /// `made` receives the new file, or stays empty when none was needed.
    bool automatic(const QString &directory, int keep = 14, qint64 minAgeSecs = 24 * 3600,
                   QString *made = nullptr);

    QString lastError() const { return m_lastError; }

private:
    Database *m_database;
    QString   m_lastError;
};

#endif // BACKUP_H
