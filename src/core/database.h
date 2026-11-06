#ifndef DATABASE_H
#define DATABASE_H

#include <QList>
#include <QString>
#include <QStringList>

/// Where the data lives. Stored in aguara.ini (see load()/save()).
struct DatabaseSettings {
    enum Backend {
        SQLite,         ///< a file on this computer: one till
        PostgreSQL      ///< a server on the LAN: several tills share the data
    };

    Backend backend = SQLite;

    // SQLite
    QString sqlitePath;         ///< empty = defaultSqlitePath()

    // PostgreSQL
    QString host;
    int     port = 5432;
    QString name = QStringLiteral("aguara");
    QString user = QStringLiteral("aguara");
    QString password;           ///< empty = let libpq use %APPDATA%\postgresql\pgpass.conf;
                                ///< saved encrypted for this Windows user (DPAPI)

    static QString settingsPath();      ///< <AppData>/aguara.ini
    static QString defaultSqlitePath(); ///< <AppData>/aguara.db
    static DatabaseSettings load();
    void save() const;

    /// "SQLite: C:\...\aguara.db" / "PostgreSQL: aguara@192.168.1.10:5432/aguara"
    QString describe() const;
};

/// One step of schema evolution. Versions are applied in order, each in its
/// own transaction, and recorded in schema_migrations, so every database
/// (new or years old) ends up with the same schema.
/// Never edit a migration that has shipped: add a new one.
struct Migration {
    int         version;
    QString     description;
    QStringList sqlite;         ///< statements for SQLite
    QStringList postgresql;     ///< statements for PostgreSQL
};

/// The application's database connection, SQLite or PostgreSQL.
/// Uses a named connection (not Qt's default one), so only code that is
/// handed the connection name — the core's services — can reach it.
class Database
{
public:
    explicit Database(const QString &connectionName = QStringLiteral("aguara"));
    ~Database();
    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;

    bool open(const DatabaseSettings &settings);
    void close();
    bool isOpen() const { return m_open; }

    /// Apply the migrations of one owner ("core", or later a module id)
    /// that this database doesn't have yet. Safe to run from several tills
    /// at once on PostgreSQL (serialized with an advisory lock).
    bool migrate(const QString &owner, const QList<Migration> &migrations);
    int  schemaVersion(const QString &owner) const;

    /// SQLite only: write a consistent copy of the open database to `file`
    /// (replaced if it exists) without stopping the program.
    bool backupTo(const QString &file);

    QString connectionName() const { return m_connectionName; }
    DatabaseSettings::Backend backend() const { return m_backend; }
    /// Why the last call failed, in Spanish for the user; details are in the log.
    QString lastError() const { return m_lastError; }

private:
    bool openSqlite(const DatabaseSettings &settings);
    bool openPostgres(const DatabaseSettings &settings);
    bool exec(const QString &sql);
    bool beginMigrationTransaction();

    QString m_connectionName;
    QString m_lastError;
    DatabaseSettings::Backend m_backend = DatabaseSettings::SQLite;
    bool    m_open = false;
};

/// The core's own schema: products, brands, categories, IVA, suppliers.
QList<Migration> coreMigrations();

/// Secrets in aguara.ini: encrypted with DPAPI, for this Windows user on this computer.
namespace AguaraSecret {
QByteArray protect(const QString &secret);
bool unprotect(const QByteArray &stored, QString *secret);
} // namespace AguaraSecret

#endif // DATABASE_H
