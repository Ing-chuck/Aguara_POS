#include "database.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QVariant>

#include <algorithm>

namespace {
// Arbitrary constant: the PostgreSQL advisory lock that serializes migrations
// when several tills start at the same time.
const int kMigrationLockKey = 0x4147; // "AG"
}

// --- DatabaseSettings -------------------------------------------------------

QString DatabaseSettings::settingsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + QStringLiteral("/aguara.ini");
}

QString DatabaseSettings::defaultSqlitePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + QStringLiteral("/aguara.db");
}

DatabaseSettings DatabaseSettings::load()
{
    DatabaseSettings s;
    QSettings ini(settingsPath(), QSettings::IniFormat);
    ini.beginGroup(QStringLiteral("database"));
    s.backend    = ini.value(QStringLiteral("backend")).toString() == QLatin1String("postgresql")
                   ? PostgreSQL : SQLite;
    s.sqlitePath = ini.value(QStringLiteral("path")).toString();
    s.host       = ini.value(QStringLiteral("host")).toString();
    s.port       = ini.value(QStringLiteral("port"), s.port).toInt();
    s.name       = ini.value(QStringLiteral("name"), s.name).toString();
    s.user       = ini.value(QStringLiteral("user"), s.user).toString();
    s.password   = ini.value(QStringLiteral("password")).toString();
    return s;
}

void DatabaseSettings::save() const
{
    QDir().mkpath(QFileInfo(settingsPath()).absolutePath());
    QSettings ini(settingsPath(), QSettings::IniFormat);
    ini.beginGroup(QStringLiteral("database"));
    ini.setValue(QStringLiteral("backend"), backend == PostgreSQL ? "postgresql" : "sqlite");
    ini.setValue(QStringLiteral("path"), sqlitePath);
    ini.setValue(QStringLiteral("host"), host);
    ini.setValue(QStringLiteral("port"), port);
    ini.setValue(QStringLiteral("name"), name);
    ini.setValue(QStringLiteral("user"), user);
    ini.setValue(QStringLiteral("password"), password);
}

QString DatabaseSettings::describe() const
{
    if (backend == SQLite)
        return QStringLiteral("SQLite: %1").arg(QDir::toNativeSeparators(
            sqlitePath.isEmpty() ? defaultSqlitePath() : sqlitePath));
    return QStringLiteral("PostgreSQL: %1@%2:%3/%4").arg(user, host).arg(port).arg(name);
}

// --- Database ---------------------------------------------------------------

Database::Database(const QString &connectionName)
    : m_connectionName(connectionName)
{
}

Database::~Database()
{
    close();
}

bool Database::open(const DatabaseSettings &settings)
{
    close();
    m_backend = settings.backend;
    const bool ok = settings.backend == DatabaseSettings::PostgreSQL ? openPostgres(settings)
                                                                     : openSqlite(settings);
    if (!ok)
        close();
    return ok;
}

bool Database::openSqlite(const DatabaseSettings &settings)
{
    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"))) {
        m_lastError = QStringLiteral("The Qt SQLite driver (qsqlite) is not available.");
        return false;
    }

    const QFileInfo file(settings.sqlitePath.isEmpty() ? DatabaseSettings::defaultSqlitePath()
                                                       : settings.sqlitePath);
    if (!QDir().mkpath(file.absolutePath())) {
        m_lastError = QStringLiteral("Cannot create folder %1").arg(file.absolutePath());
        return false;
    }

    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
        m_open = true;      // from here on close() must remove the connection
        db.setDatabaseName(file.absoluteFilePath());
        if (!db.open()) {
            m_lastError = db.lastError().text();
            return false;
        }
    }

    // foreign_keys: off by default in SQLite, must be enabled per connection.
    // WAL + synchronous=FULL: readers don't block the writer, and a committed
    // sale survives a power cut. (WAL needs a local disk: never put the file
    // on a network share; use the PostgreSQL backend to share data.)
    // busy_timeout: wait instead of failing if another process holds a lock.
    return exec(QStringLiteral("PRAGMA foreign_keys = ON"))
        && exec(QStringLiteral("PRAGMA journal_mode = WAL"))
        && exec(QStringLiteral("PRAGMA synchronous = FULL"))
        && exec(QStringLiteral("PRAGMA busy_timeout = 5000"));
}

bool Database::openPostgres(const DatabaseSettings &settings)
{
    if (settings.host.trimmed().isEmpty()) {
        m_lastError = QStringLiteral("No PostgreSQL server configured (host is empty).");
        return false;
    }

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QPSQL"), m_connectionName);
    m_open = true;
    if (!db.isValid()) {
        // The Qt plugin loads, but libpq.dll (or one of its DLLs) is missing.
        m_lastError = QStringLiteral(
            "PostgreSQL support is not installed on this computer: libpq.dll and its "
            "DLLs (libssl-1_1, libcrypto-1_1, libintl-8, libiconv-2) must be next to "
            "aguara.exe, and the Visual C++ 2013 (x86) runtime installed.");
        return false;
    }

    db.setHostName(settings.host.trimmed());
    db.setPort(settings.port);
    db.setDatabaseName(settings.name);
    db.setUserName(settings.user);
    if (!settings.password.isEmpty())
        db.setPassword(settings.password);
    // fail fast if the server is unreachable, and name this client in pg_stat_activity
    db.setConnectOptions(QStringLiteral("connect_timeout=5;application_name=AguaraPOS"));
    if (!db.open()) {
        m_lastError = db.lastError().text();
        return false;
    }
    return true;
}

void Database::close()
{
    if (!m_open)
        return;
    {
        QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
        db.close();
    }
    QSqlDatabase::removeDatabase(m_connectionName);
    m_open = false;
}

bool Database::exec(const QString &sql)
{
    QSqlQuery q(QSqlDatabase::database(m_connectionName));
    if (!q.exec(sql)) {
        m_lastError = QStringLiteral("%1\n(%2)").arg(q.lastError().text(), sql.simplified());
        return false;
    }
    return true;
}

int Database::schemaVersion(const QString &owner) const
{
    QSqlQuery q(QSqlDatabase::database(m_connectionName));
    q.prepare(QStringLiteral("SELECT MAX(version) FROM schema_migrations WHERE owner = ?"));
    q.addBindValue(owner);
    if (q.exec() && q.next())
        return q.value(0).toInt();    // NULL (no rows) -> 0
    return 0;
}

/// Begin a transaction; on PostgreSQL also take the migration lock, which is
/// released automatically at commit/rollback.
bool Database::beginMigrationTransaction()
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    if (!db.transaction()) {
        m_lastError = db.lastError().text();
        return false;
    }
    if (m_backend == DatabaseSettings::PostgreSQL
        && !exec(QStringLiteral("SELECT pg_advisory_xact_lock(%1)").arg(kMigrationLockKey))) {
        db.rollback();
        return false;
    }
    return true;
}

bool Database::migrate(const QString &owner, const QList<Migration> &migrations)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);

    if (!beginMigrationTransaction())
        return false;
    if (!exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS schema_migrations ("
            " owner       TEXT    NOT NULL,"
            " version     INTEGER NOT NULL,"
            " description TEXT,"
            " applied_at  TEXT    NOT NULL,"
            " PRIMARY KEY (owner, version))"))
        || !db.commit()) {
        db.rollback();
        return false;
    }

    QList<Migration> pending = migrations;
    std::sort(pending.begin(), pending.end(),
              [](const Migration &a, const Migration &b) { return a.version < b.version; });

    for (const Migration &m : qAsConst(pending)) {
        if (!beginMigrationTransaction())
            return false;

        // Re-read under the lock: another till may have just applied it.
        if (m.version <= schemaVersion(owner)) {
            db.commit();
            continue;
        }

        const QStringList &statements = m_backend == DatabaseSettings::PostgreSQL ? m.postgresql : m.sqlite;
        bool ok = !statements.isEmpty();
        if (!ok)
            m_lastError = QStringLiteral("no statements for this database type");
        for (const QString &statement : statements) {
            if (!exec(statement)) {
                ok = false;
                break;
            }
        }

        if (ok) {
            QSqlQuery q(db);
            q.prepare(QStringLiteral("INSERT INTO schema_migrations (owner, version, description, applied_at)"
                                     " VALUES (?, ?, ?, ?)"));
            q.addBindValue(owner);
            q.addBindValue(m.version);
            q.addBindValue(m.description);
            q.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
            ok = q.exec();
            if (!ok)
                m_lastError = q.lastError().text();
        }

        if (!ok) {
            db.rollback();
            m_lastError = QStringLiteral("Migration %1 v%2 (%3) failed: %4")
                              .arg(owner).arg(m.version).arg(m.description, m_lastError);
            return false;
        }
        if (!db.commit()) {
            m_lastError = db.lastError().text();
            return false;
        }
    }
    return true;
}

QList<Migration> coreMigrations()
{
    // Table and column names follow doc/arquitectura/db/test_database.graphml.
    // Money columns hold whole guaraníes (see interfaces/money.h): INTEGER in
    // SQLite is 64-bit; in PostgreSQL that is BIGINT.
    // Names are unique ignoring case: COLLATE NOCASE in SQLite, an index on
    // lower(name) in PostgreSQL. Queries compare with lower() on both.
    Migration v1;
    v1.version = 1;
    v1.description = QStringLiteral("products, brands, categories, IVA, suppliers");

    v1.sqlite = QStringList {
        QStringLiteral(
            "CREATE TABLE marcas ("
            " id        INTEGER PRIMARY KEY,"
            " nombre    TEXT    NOT NULL UNIQUE COLLATE NOCASE,"
            " es_activo INTEGER NOT NULL DEFAULT 1)"),
        QStringLiteral(
            "CREATE TABLE rubros ("
            " id        INTEGER PRIMARY KEY,"
            " rubro     TEXT    NOT NULL UNIQUE COLLATE NOCASE,"
            " parent_id INTEGER REFERENCES rubros(id),"
            " es_activo INTEGER NOT NULL DEFAULT 1)"),
        // IVA rates in force in Paraguay: 10 %, 5 %, exempt.
        QStringLiteral(
            "CREATE TABLE iva_cat ("
            " tasa      INTEGER PRIMARY KEY,"     // percent
            " categoria TEXT    NOT NULL)"),
        QStringLiteral("INSERT INTO iva_cat (tasa, categoria) VALUES"
                       " (10, 'IVA 10%'), (5, 'IVA 5%'), (0, 'Exenta')"),
        QStringLiteral(
            "CREATE TABLE proveedores ("
            " id             INTEGER PRIMARY KEY,"
            " nombre         TEXT    NOT NULL,"
            " direccion      TEXT,"
            " telefono       TEXT,"
            " email          TEXT,"
            " otros_detalles TEXT,"
            " contacto       TEXT,"
            " web            TEXT,"
            " es_activo      INTEGER NOT NULL DEFAULT 1)"),
        QStringLiteral(
            "CREATE TABLE productos ("
            " codigo       TEXT    PRIMARY KEY,"   // text: barcodes keep leading zeros
            " descripcion  TEXT    NOT NULL,"
            " precio       INTEGER NOT NULL DEFAULT 0 CHECK (precio >= 0),"  // Gs., IVA included
            " costo        INTEGER NOT NULL DEFAULT 0 CHECK (costo >= 0),"   // Gs.
            " stock        INTEGER NOT NULL DEFAULT 0,"
            " stock_min    INTEGER NOT NULL DEFAULT 0,"
            " f_compra     TEXT,"                  // ISO date
            " f_mod        TEXT,"                  // ISO date
            " es_activo    INTEGER NOT NULL DEFAULT 1,"
            " marca_id     INTEGER REFERENCES marcas(id),"
            " rubro_id     INTEGER REFERENCES rubros(id),"
            " iva          INTEGER NOT NULL DEFAULT 10 REFERENCES iva_cat(tasa),"
            " proveedor_id INTEGER REFERENCES proveedores(id))"),
        QStringLiteral("CREATE INDEX productos_descripcion ON productos (descripcion)"),
    };

    v1.postgresql = QStringList {
        QStringLiteral(
            "CREATE TABLE marcas ("
            " id        BIGINT  GENERATED BY DEFAULT AS IDENTITY PRIMARY KEY,"
            " nombre    TEXT    NOT NULL,"
            " es_activo INTEGER NOT NULL DEFAULT 1)"),
        QStringLiteral("CREATE UNIQUE INDEX marcas_nombre_key ON marcas (lower(nombre))"),
        QStringLiteral(
            "CREATE TABLE rubros ("
            " id        BIGINT  GENERATED BY DEFAULT AS IDENTITY PRIMARY KEY,"
            " rubro     TEXT    NOT NULL,"
            " parent_id BIGINT  REFERENCES rubros(id),"
            " es_activo INTEGER NOT NULL DEFAULT 1)"),
        QStringLiteral("CREATE UNIQUE INDEX rubros_rubro_key ON rubros (lower(rubro))"),
        QStringLiteral(
            "CREATE TABLE iva_cat ("
            " tasa      INTEGER PRIMARY KEY,"
            " categoria TEXT    NOT NULL)"),
        QStringLiteral("INSERT INTO iva_cat (tasa, categoria) VALUES"
                       " (10, 'IVA 10%'), (5, 'IVA 5%'), (0, 'Exenta')"),
        QStringLiteral(
            "CREATE TABLE proveedores ("
            " id             BIGINT  GENERATED BY DEFAULT AS IDENTITY PRIMARY KEY,"
            " nombre         TEXT    NOT NULL,"
            " direccion      TEXT,"
            " telefono       TEXT,"
            " email          TEXT,"
            " otros_detalles TEXT,"
            " contacto       TEXT,"
            " web            TEXT,"
            " es_activo      INTEGER NOT NULL DEFAULT 1)"),
        QStringLiteral(
            "CREATE TABLE productos ("
            " codigo       TEXT    PRIMARY KEY,"
            " descripcion  TEXT    NOT NULL,"
            " precio       BIGINT  NOT NULL DEFAULT 0 CHECK (precio >= 0),"
            " costo        BIGINT  NOT NULL DEFAULT 0 CHECK (costo >= 0),"
            " stock        INTEGER NOT NULL DEFAULT 0,"
            " stock_min    INTEGER NOT NULL DEFAULT 0,"
            " f_compra     DATE,"
            " f_mod        DATE,"
            " es_activo    INTEGER NOT NULL DEFAULT 1,"
            " marca_id     BIGINT  REFERENCES marcas(id),"
            " rubro_id     BIGINT  REFERENCES rubros(id),"
            " iva          INTEGER NOT NULL DEFAULT 10 REFERENCES iva_cat(tasa),"
            " proveedor_id BIGINT  REFERENCES proveedores(id))"),
        QStringLiteral("CREATE INDEX productos_descripcion ON productos (descripcion)"),
    };

    return { v1 };
}
