#include "database.h"
#include "errors.h"
#include "logging.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QVariant>

#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#endif

namespace {
// Arbitrary constant: the PostgreSQL advisory lock that serializes migrations
// when several tills start at the same time.
const int kMigrationLockKey = 0x4147; // "AG"

QString tr(const char *text)
{
    return QCoreApplication::translate("Database", text);
}

// The database password is kept encrypted with Windows DPAPI: only the same
// Windows user on the same computer can decrypt it, so a copy of aguara.ini
// doesn't reveal it. (On other systems it is only base64-encoded.)
const char kPasswordKey[]          = "password";            // older versions: plain text
const char kProtectedPasswordKey[] = "password_protected";  // DPAPI, base64

QByteArray protect(const QString &password)
{
#ifdef Q_OS_WIN
    QByteArray plain = password.toUtf8();
    DATA_BLOB in;
    in.pbData = reinterpret_cast<BYTE *>(plain.data());
    in.cbData = DWORD(plain.size());
    DATA_BLOB out = {};
    if (!CryptProtectData(&in, L"AguaraPOS", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        qCWarning(lcApp) << "CryptProtectData failed, error" << GetLastError();
        return QByteArray();
    }
    const QByteArray result(reinterpret_cast<const char *>(out.pbData), int(out.cbData));
    LocalFree(out.pbData);
    return result.toBase64();
#else
    return password.toUtf8().toBase64();
#endif
}

bool unprotect(const QByteArray &stored, QString *password)
{
    QByteArray blob = QByteArray::fromBase64(stored);
#ifdef Q_OS_WIN
    DATA_BLOB in;
    in.pbData = reinterpret_cast<BYTE *>(blob.data());
    in.cbData = DWORD(blob.size());
    DATA_BLOB out = {};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        qCWarning(lcApp) << "The saved database password can't be decrypted (aguara.ini copied from"
                            " another computer or Windows user?), error" << GetLastError();
        return false;
    }
    *password = QString::fromUtf8(reinterpret_cast<const char *>(out.pbData), int(out.cbData));
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return true;
#else
    *password = QString::fromUtf8(blob);
    return true;
#endif
}
} // namespace

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
    if (ini.contains(QLatin1String(kProtectedPasswordKey))) {
        unprotect(ini.value(QLatin1String(kProtectedPasswordKey)).toByteArray(), &s.password);
    } else if (ini.contains(QLatin1String(kPasswordKey))) {
        // written in plain text by an older version: encrypt it now
        s.password = ini.value(QLatin1String(kPasswordKey)).toString();
        ini.remove(QLatin1String(kPasswordKey));
        if (!s.password.isEmpty())
            ini.setValue(QLatin1String(kProtectedPasswordKey), protect(s.password));
    }
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
    ini.remove(QLatin1String(kPasswordKey));
    if (password.isEmpty())
        ini.remove(QLatin1String(kProtectedPasswordKey));
    else
        ini.setValue(QLatin1String(kProtectedPasswordKey), protect(password));
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
        m_lastError = tr("Falta el componente de SQLite (qsqlite) en la instalación. Reinstale AguaraPOS.");
        return false;
    }

    const QFileInfo file(settings.sqlitePath.isEmpty() ? DatabaseSettings::defaultSqlitePath()
                                                       : settings.sqlitePath);
    if (!QDir().mkpath(file.absolutePath())) {
        m_lastError = tr("No se puede crear la carpeta %1.").arg(QDir::toNativeSeparators(file.absolutePath()));
        return false;
    }

    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
        m_open = true;      // from here on close() must remove the connection
        db.setDatabaseName(file.absoluteFilePath());
        if (!db.open()) {
            m_lastError = AguaraErrors::logAndExplain(db.lastError(),
                QStringLiteral("Opening SQLite file %1").arg(file.absoluteFilePath()));
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
        m_lastError = tr("No se indicó el servidor de datos.");
        return false;
    }

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QPSQL"), m_connectionName);
    m_open = true;
    if (!db.isValid()) {
        // The Qt plugin loads, but libpq.dll (or one of its DLLs) is missing.
        qCWarning(lcDatabase) << "QPSQL driver not usable: libpq.dll or one of its DLLs"
                                 " (libssl-1_1, libcrypto-1_1, libintl-8, libiconv-2) or the"
                                 " Visual C++ 2013 x86 runtime is missing";
        m_lastError = tr("Falta en este equipo el componente para conectarse al servidor (PostgreSQL). "
                         "Ver la guía de instalación de varios puestos.");
        return false;
    }

    db.setHostName(settings.host.trimmed());
    db.setPort(settings.port);
    db.setDatabaseName(settings.name);
    db.setUserName(settings.user);
    if (!settings.password.isEmpty())
        db.setPassword(settings.password);
    // fail fast if the server is unreachable, and name this client in pg_stat_activity.
    // TCP keepalives: a connection whose network went away (cable, switch,
    // server PC powered off) errors out in about a minute instead of waiting for the
    // operating system's much longer timeout.
    db.setConnectOptions(QStringLiteral("connect_timeout=5;application_name=AguaraPOS;"
                                        "keepalives=1;keepalives_idle=10;keepalives_interval=5"));
    if (!db.open()) {
        m_lastError = AguaraErrors::logAndExplain(db.lastError(),
            QStringLiteral("Connecting to %1").arg(settings.describe()));
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
        m_lastError = AguaraErrors::logAndExplain(q.lastError(), QStringLiteral("SQL: %1").arg(sql.simplified()));
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
        m_lastError = AguaraErrors::logAndExplain(db.lastError(), QStringLiteral("BEGIN (migration)"));
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
            qCWarning(lcDatabase) << "Migration" << owner << m.version << "has no statements for this database type";
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
                m_lastError = AguaraErrors::logAndExplain(q.lastError(), QStringLiteral("Recording migration"));
        }

        if (!ok) {
            db.rollback();
            qCWarning(lcDatabase) << "Migration" << owner << m.version << m.description << "failed";
            m_lastError = tr("No se pudo actualizar la estructura de la base de datos (%1, versión %2). %3")
                              .arg(owner).arg(m.version).arg(m_lastError);
            return false;
        }
        if (!db.commit()) {
            m_lastError = AguaraErrors::logAndExplain(db.lastError(), QStringLiteral("COMMIT (migration)"));
            return false;
        }
        qCInfo(lcDatabase) << "Applied migration" << owner << m.version << m.description;
    }
    return true;
}

bool Database::backupTo(const QString &file)
{
    if (!m_open || m_backend != DatabaseSettings::SQLite) {
        m_lastError = tr("La copia de seguridad desde el programa es solo para datos guardados en este "
                         "equipo. Con servidor, la copia se hace en el servidor.");
        return false;
    }
    const QFileInfo target(file);
    if (!QDir().mkpath(target.absolutePath())) {
        m_lastError = tr("No se puede crear la carpeta %1.").arg(QDir::toNativeSeparators(target.absolutePath()));
        return false;
    }
    if (target.exists() && !QFile::remove(target.absoluteFilePath())) {
        m_lastError = tr("No se puede reemplazar el archivo %1.").arg(QDir::toNativeSeparators(file));
        return false;
    }
    // VACUUM INTO writes a consistent, compacted copy while the program keeps
    // working (SQLite 3.27+; Qt 5.15 ships 3.33).
    QSqlQuery q(QSqlDatabase::database(m_connectionName, false));
    q.prepare(QStringLiteral("VACUUM INTO ?"));
    q.addBindValue(target.absoluteFilePath());
    if (!q.exec()) {
        m_lastError = AguaraErrors::logAndExplain(q.lastError(), QStringLiteral("Backup to %1").arg(file));
        return false;
    }
    qCInfo(lcApp) << "Database backed up to" << target.absoluteFilePath();
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

    // Users, roles, permissions and the audit log. Permissions are registered
    // at startup (core + modules); the three roles are created here.
    Migration v2;
    v2.version = 2;
    v2.description = QStringLiteral("users, roles, permissions, audit log");

    const QStringList seedRoles = {
        QStringLiteral("INSERT INTO roles (nombre, descripcion, todos_los_permisos, es_sistema) VALUES"
                       " ('Administrador', 'Todo el sistema, usuarios incluidos', 1, 1),"
                       " ('Encargado', 'Inventario, reportes y autorizaciones', 0, 1),"
                       " ('Cajero', 'Ventas y caja', 0, 1)"),
    };

    v2.sqlite = QStringList {
        QStringLiteral(
            "CREATE TABLE usuarios ("
            " id                 INTEGER PRIMARY KEY,"
            " usuario            TEXT    NOT NULL UNIQUE COLLATE NOCASE,"
            " nombre             TEXT    NOT NULL,"
            " clave              TEXT    NOT NULL,"       // see passwordhash.h
            " es_activo          INTEGER NOT NULL DEFAULT 1,"
            " debe_cambiar_clave INTEGER NOT NULL DEFAULT 0,"
            " intentos_fallidos  INTEGER NOT NULL DEFAULT 0,"
            " bloqueado_hasta    TEXT)"),                 // ISO date-time
        QStringLiteral(
            "CREATE TABLE roles ("
            " id                 INTEGER PRIMARY KEY,"
            " nombre             TEXT    NOT NULL UNIQUE COLLATE NOCASE,"
            " descripcion        TEXT,"
            " todos_los_permisos INTEGER NOT NULL DEFAULT 0,"
            " es_sistema         INTEGER NOT NULL DEFAULT 0)"),
        QStringLiteral(
            "CREATE TABLE permisos ("
            " codigo      TEXT PRIMARY KEY,"
            " descripcion TEXT NOT NULL,"
            " grupo       TEXT NOT NULL)"),
        QStringLiteral(
            "CREATE TABLE rol_tiene_permisos ("
            " rol_id  INTEGER NOT NULL REFERENCES roles(id) ON DELETE CASCADE,"
            " permiso TEXT    NOT NULL REFERENCES permisos(codigo),"
            " PRIMARY KEY (rol_id, permiso))"),
        QStringLiteral(
            "CREATE TABLE usuario_tiene_roles ("
            " usuario_id INTEGER NOT NULL REFERENCES usuarios(id),"
            " rol_id     INTEGER NOT NULL REFERENCES roles(id),"
            " PRIMARY KEY (usuario_id, rol_id))"),
        QStringLiteral(
            "CREATE TABLE auditoria ("
            " id             INTEGER PRIMARY KEY,"
            " fecha          TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%S', 'now', 'localtime')),"
            " usuario_id     INTEGER,"                    // no foreign key: the record outlives everything
            " usuario        TEXT,"
            " autorizado_por TEXT,"
            " puesto         TEXT,"
            " accion         TEXT NOT NULL,"
            " entidad        TEXT,"
            " entidad_id     TEXT,"
            " antes          TEXT,"
            " despues        TEXT)"),
        QStringLiteral("CREATE INDEX auditoria_fecha ON auditoria (fecha)"),
    };
    v2.sqlite += seedRoles;

    v2.postgresql = QStringList {
        QStringLiteral(
            "CREATE TABLE usuarios ("
            " id                 BIGINT  GENERATED BY DEFAULT AS IDENTITY PRIMARY KEY,"
            " usuario            TEXT    NOT NULL,"
            " nombre             TEXT    NOT NULL,"
            " clave              TEXT    NOT NULL,"
            " es_activo          INTEGER NOT NULL DEFAULT 1,"
            " debe_cambiar_clave INTEGER NOT NULL DEFAULT 0,"
            " intentos_fallidos  INTEGER NOT NULL DEFAULT 0,"
            " bloqueado_hasta    TEXT)"),
        QStringLiteral("CREATE UNIQUE INDEX usuarios_usuario_key ON usuarios (lower(usuario))"),
        QStringLiteral(
            "CREATE TABLE roles ("
            " id                 BIGINT  GENERATED BY DEFAULT AS IDENTITY PRIMARY KEY,"
            " nombre             TEXT    NOT NULL,"
            " descripcion        TEXT,"
            " todos_los_permisos INTEGER NOT NULL DEFAULT 0,"
            " es_sistema         INTEGER NOT NULL DEFAULT 0)"),
        QStringLiteral("CREATE UNIQUE INDEX roles_nombre_key ON roles (lower(nombre))"),
        QStringLiteral(
            "CREATE TABLE permisos ("
            " codigo      TEXT PRIMARY KEY,"
            " descripcion TEXT NOT NULL,"
            " grupo       TEXT NOT NULL)"),
        QStringLiteral(
            "CREATE TABLE rol_tiene_permisos ("
            " rol_id  BIGINT NOT NULL REFERENCES roles(id) ON DELETE CASCADE,"
            " permiso TEXT   NOT NULL REFERENCES permisos(codigo),"
            " PRIMARY KEY (rol_id, permiso))"),
        QStringLiteral(
            "CREATE TABLE usuario_tiene_roles ("
            " usuario_id BIGINT NOT NULL REFERENCES usuarios(id),"
            " rol_id     BIGINT NOT NULL REFERENCES roles(id),"
            " PRIMARY KEY (usuario_id, rol_id))"),
        // the server's clock: a till can't backdate its records
        QStringLiteral(
            "CREATE TABLE auditoria ("
            " id             BIGINT GENERATED BY DEFAULT AS IDENTITY PRIMARY KEY,"
            " fecha          TIMESTAMPTZ NOT NULL DEFAULT now(),"
            " usuario_id     BIGINT,"
            " usuario        TEXT,"
            " autorizado_por TEXT,"
            " puesto         TEXT,"
            " accion         TEXT NOT NULL,"
            " entidad        TEXT,"
            " entidad_id     TEXT,"
            " antes          TEXT,"
            " despues        TEXT)"),
        QStringLiteral("CREATE INDEX auditoria_fecha ON auditoria (fecha)"),
    };
    v2.postgresql += seedRoles;

    // Inventory: stock as a ledger, thousandths (see interfaces/quantity.h),
    // units, alternate barcodes, price history, purchases and counts.
    Migration v3;
    v3.version = 3;
    v3.description = QStringLiteral("stock movements, units, barcodes, price history, purchases, counts");

    const QStringList commonBefore = {
        QStringLiteral(
            "CREATE TABLE unidades_basicas ("
            " id          INTEGER PRIMARY KEY,"
            " unidad      TEXT    NOT NULL,"
            " abreviatura TEXT    NOT NULL,"
            " decimales   INTEGER NOT NULL)"),
        QStringLiteral("INSERT INTO unidades_basicas (id, unidad, abreviatura, decimales) VALUES"
                       " (1, 'Unidad', 'un', 0), (2, 'Kilogramo', 'kg', 3),"
                       " (3, 'Litro', 'l', 3), (4, 'Metro', 'm', 3)"),
    };
    // existing stock in thousandths, and its opening movement: from here on
    // the stock of any article is the sum of its movements
    const QStringList commonAfter = {
        QStringLiteral("UPDATE productos SET stock = stock * 1000, stock_min = stock_min * 1000, unidad_id = 1"),
        QStringLiteral("INSERT INTO stock_movimientos (producto_codigo, tipo, cantidad, saldo, costo_unitario, motivo)"
                       " SELECT codigo, 'inicial', stock, stock, costo, 'Saldo inicial'"
                       " FROM productos WHERE stock <> 0"),
    };

    v3.sqlite = commonBefore + QStringList {
        QStringLiteral("ALTER TABLE productos ADD COLUMN unidad_id INTEGER REFERENCES unidades_basicas(id)"),
        QStringLiteral(
            "CREATE TABLE stock_movimientos ("
            " id              INTEGER PRIMARY KEY,"
            " fecha           TEXT    NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%S', 'now', 'localtime')),"
            " producto_codigo TEXT    NOT NULL REFERENCES productos(codigo) ON UPDATE CASCADE,"
            " tipo            TEXT    NOT NULL,"
            " cantidad        INTEGER NOT NULL,"
            " saldo           INTEGER NOT NULL,"
            " costo_unitario  INTEGER NOT NULL DEFAULT 0,"
            " motivo          TEXT,"
            " documento       TEXT,"
            " usuario         TEXT,"
            " puesto          TEXT)"),
        QStringLiteral("CREATE INDEX stock_movimientos_producto ON stock_movimientos (producto_codigo, id)"),
        QStringLiteral(
            "CREATE TABLE codigos_barras ("
            " codigo          TEXT PRIMARY KEY,"
            " producto_codigo TEXT NOT NULL REFERENCES productos(codigo) ON UPDATE CASCADE)"),
        QStringLiteral(
            "CREATE TABLE precios_historial ("
            " id              INTEGER PRIMARY KEY,"
            " fecha           TEXT    NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%S', 'now', 'localtime')),"
            " producto_codigo TEXT    NOT NULL REFERENCES productos(codigo) ON UPDATE CASCADE,"
            " precio_anterior INTEGER NOT NULL,"
            " precio_nuevo    INTEGER NOT NULL,"
            " costo_anterior  INTEGER NOT NULL,"
            " costo_nuevo     INTEGER NOT NULL,"
            " motivo          TEXT,"
            " usuario         TEXT)"),
        QStringLiteral("CREATE INDEX precios_historial_producto ON precios_historial (producto_codigo, id)"),
        QStringLiteral(
            "CREATE TABLE compras ("
            " id           INTEGER PRIMARY KEY,"
            " fecha        TEXT    NOT NULL,"
            " proveedor_id INTEGER NOT NULL REFERENCES proveedores(id),"
            " factura      TEXT,"
            " total        INTEGER NOT NULL,"
            " usuario      TEXT)"),
        QStringLiteral(
            "CREATE TABLE compra_items ("
            " id              INTEGER PRIMARY KEY,"
            " compra_id       INTEGER NOT NULL REFERENCES compras(id),"
            " producto_codigo TEXT    NOT NULL REFERENCES productos(codigo) ON UPDATE CASCADE,"
            " cantidad        INTEGER NOT NULL,"
            " costo_unitario  INTEGER NOT NULL,"
            " precio_nuevo    INTEGER NOT NULL DEFAULT 0)"),
    } + commonAfter;

    v3.postgresql = commonBefore + QStringList {
        QStringLiteral("ALTER TABLE productos ADD COLUMN unidad_id INTEGER REFERENCES unidades_basicas(id)"),
        QStringLiteral("ALTER TABLE productos ALTER COLUMN stock TYPE BIGINT"),
        QStringLiteral("ALTER TABLE productos ALTER COLUMN stock_min TYPE BIGINT"),
        QStringLiteral(
            "CREATE TABLE stock_movimientos ("
            " id              BIGINT GENERATED BY DEFAULT AS IDENTITY PRIMARY KEY,"
            " fecha           TIMESTAMPTZ NOT NULL DEFAULT now(),"
            " producto_codigo TEXT   NOT NULL REFERENCES productos(codigo) ON UPDATE CASCADE,"
            " tipo            TEXT   NOT NULL,"
            " cantidad        BIGINT NOT NULL,"
            " saldo           BIGINT NOT NULL,"
            " costo_unitario  BIGINT NOT NULL DEFAULT 0,"
            " motivo          TEXT,"
            " documento       TEXT,"
            " usuario         TEXT,"
            " puesto          TEXT)"),
        QStringLiteral("CREATE INDEX stock_movimientos_producto ON stock_movimientos (producto_codigo, id)"),
        QStringLiteral(
            "CREATE TABLE codigos_barras ("
            " codigo          TEXT PRIMARY KEY,"
            " producto_codigo TEXT NOT NULL REFERENCES productos(codigo) ON UPDATE CASCADE)"),
        QStringLiteral(
            "CREATE TABLE precios_historial ("
            " id              BIGINT GENERATED BY DEFAULT AS IDENTITY PRIMARY KEY,"
            " fecha           TIMESTAMPTZ NOT NULL DEFAULT now(),"
            " producto_codigo TEXT   NOT NULL REFERENCES productos(codigo) ON UPDATE CASCADE,"
            " precio_anterior BIGINT NOT NULL,"
            " precio_nuevo    BIGINT NOT NULL,"
            " costo_anterior  BIGINT NOT NULL,"
            " costo_nuevo     BIGINT NOT NULL,"
            " motivo          TEXT,"
            " usuario         TEXT)"),
        QStringLiteral("CREATE INDEX precios_historial_producto ON precios_historial (producto_codigo, id)"),
        QStringLiteral(
            "CREATE TABLE compras ("
            " id           BIGINT GENERATED BY DEFAULT AS IDENTITY PRIMARY KEY,"
            " fecha        DATE   NOT NULL,"
            " proveedor_id BIGINT NOT NULL REFERENCES proveedores(id),"
            " factura      TEXT,"
            " total        BIGINT NOT NULL,"
            " usuario      TEXT)"),
        QStringLiteral(
            "CREATE TABLE compra_items ("
            " id              BIGINT GENERATED BY DEFAULT AS IDENTITY PRIMARY KEY,"
            " compra_id       BIGINT NOT NULL REFERENCES compras(id),"
            " producto_codigo TEXT   NOT NULL REFERENCES productos(codigo) ON UPDATE CASCADE,"
            " cantidad        BIGINT NOT NULL,"
            " costo_unitario  BIGINT NOT NULL,"
            " precio_nuevo    BIGINT NOT NULL DEFAULT 0)"),
    } + commonAfter;

    // Registers, cash sessions and sales, and how much each role may discount.
    // Numbers are consecutive per register: cajas.ultimo_numero, moved inside
    // the sale's transaction.
    Migration v4;
    v4.version = 4;
    v4.description = QStringLiteral("cash registers, sessions, sales, payments, held sales");

    const auto salesSchema = [](bool postgres) {
        const QString id = postgres ? QStringLiteral("BIGINT GENERATED BY DEFAULT AS IDENTITY PRIMARY KEY")
                                    : QStringLiteral("INTEGER PRIMARY KEY");
        const QString big = postgres ? QStringLiteral("BIGINT") : QStringLiteral("INTEGER");
        const QString time = postgres ? QStringLiteral("TIMESTAMPTZ") : QStringLiteral("TEXT");
        const QString now = postgres ? QStringLiteral("now()")
                                     : QStringLiteral("(strftime('%Y-%m-%dT%H:%M:%S', 'now', 'localtime'))");
        const QStringList tables = {
            QStringLiteral(
                "CREATE TABLE cajas ("
                " id            $ID,"
                " nombre        TEXT NOT NULL,"
                " puesto        TEXT NOT NULL UNIQUE,"
                " ultimo_numero $BIG NOT NULL DEFAULT 0)"),
            QStringLiteral(
                "CREATE TABLE sesiones_caja ("
                " id               $ID,"
                " caja_id          $BIG NOT NULL REFERENCES cajas(id),"
                " estado           TEXT NOT NULL DEFAULT 'abierta',"
                " apertura         $TIME NOT NULL DEFAULT $NOW,"
                " usuario_apertura TEXT,"
                " fondo            $BIG NOT NULL,"
                " cierre           $TIME,"
                " usuario_cierre   TEXT,"
                " esperado         $BIG NOT NULL DEFAULT 0,"
                " contado          $BIG NOT NULL DEFAULT 0,"
                " diferencia       $BIG NOT NULL DEFAULT 0,"
                " motivo           TEXT,"
                " autorizado_por   TEXT)"),
            QStringLiteral("CREATE UNIQUE INDEX sesiones_caja_una_abierta ON sesiones_caja (caja_id) WHERE estado = 'abierta'"),
            QStringLiteral(
                "CREATE TABLE sesion_conteos ("
                " sesion_id    $BIG NOT NULL REFERENCES sesiones_caja(id),"
                " momento      TEXT NOT NULL,"
                " denominacion $BIG NOT NULL,"
                " cantidad     INTEGER NOT NULL,"
                " PRIMARY KEY (sesion_id, momento, denominacion))"),
            QStringLiteral(
                "CREATE TABLE caja_movimientos ("
                " id        $ID,"
                " sesion_id $BIG NOT NULL REFERENCES sesiones_caja(id),"
                " fecha     $TIME NOT NULL DEFAULT $NOW,"
                " tipo      TEXT NOT NULL,"
                " monto     $BIG NOT NULL,"
                " motivo    TEXT NOT NULL,"
                " usuario   TEXT)"),
            QStringLiteral("CREATE INDEX caja_movimientos_sesion ON caja_movimientos (sesion_id)"),
            QStringLiteral(
                "CREATE TABLE ventas ("
                " id            $ID,"
                " caja_id       $BIG NOT NULL REFERENCES cajas(id),"
                " sesion_id     $BIG NOT NULL REFERENCES sesiones_caja(id),"
                " numero        $BIG NOT NULL,"
                " fecha         $TIME NOT NULL DEFAULT $NOW,"
                " usuario       TEXT,"
                " estado        TEXT NOT NULL,"
                " bruto         $BIG NOT NULL,"
                " descuento     $BIG NOT NULL,"
                " neto          $BIG NOT NULL,"
                " redondeo      $BIG NOT NULL,"
                " total         $BIG NOT NULL,"
                " gravado10     $BIG NOT NULL,"
                " iva10         $BIG NOT NULL,"
                " gravado5      $BIG NOT NULL,"
                " iva5          $BIG NOT NULL,"
                " exento        $BIG NOT NULL,"
                " anulada_por   TEXT,"
                " anulada_fecha $TIME,"
                " motivo        TEXT,"
                " UNIQUE (caja_id, numero))"),
            QStringLiteral("CREATE INDEX ventas_sesion ON ventas (sesion_id)"),
            QStringLiteral(
                "CREATE TABLE ventas_items ("
                " id              $ID,"
                " venta_id        $BIG NOT NULL REFERENCES ventas(id),"
                " linea           INTEGER NOT NULL,"
                " producto_codigo TEXT NOT NULL REFERENCES productos(codigo) ON UPDATE CASCADE,"
                " descripcion     TEXT NOT NULL,"
                " unidad          TEXT,"
                " decimales       INTEGER NOT NULL DEFAULT 0,"
                " cantidad        $BIG NOT NULL,"
                " precio_unitario $BIG NOT NULL,"
                " bruto           $BIG NOT NULL,"
                " descuento       $BIG NOT NULL,"
                " importe         $BIG NOT NULL,"
                " iva_tasa        INTEGER NOT NULL,"
                " iva             $BIG NOT NULL,"
                " costo_unitario  $BIG NOT NULL)"),
            QStringLiteral("CREATE INDEX ventas_items_venta ON ventas_items (venta_id)"),
            QStringLiteral(
                "CREATE TABLE ventas_pagos ("
                " id         $ID,"
                " venta_id   $BIG NOT NULL REFERENCES ventas(id),"
                " medio      TEXT NOT NULL,"
                " monto      $BIG NOT NULL,"
                " vuelto     $BIG NOT NULL DEFAULT 0,"
                " referencia TEXT)"),
            QStringLiteral("CREATE INDEX ventas_pagos_venta ON ventas_pagos (venta_id)"),
            QStringLiteral(
                "CREATE TABLE ventas_espera ("
                " id      $ID,"
                " caja_id $BIG NOT NULL REFERENCES cajas(id),"
                " fecha   $TIME NOT NULL DEFAULT $NOW,"
                " usuario TEXT,"
                " datos   TEXT NOT NULL)"),
            QStringLiteral("ALTER TABLE roles ADD COLUMN descuento_max INTEGER NOT NULL DEFAULT 0"),
            QStringLiteral("UPDATE roles SET descuento_max = 100 WHERE todos_los_permisos = 1"),
            QStringLiteral("UPDATE roles SET descuento_max = 20 WHERE nombre = 'Encargado'"),
            QStringLiteral("UPDATE roles SET descuento_max = 5 WHERE nombre = 'Cajero'"),
        };
        QStringList result;
        for (QString sql : tables)
            result << sql.replace(QLatin1String("$ID"), id).replace(QLatin1String("$BIG"), big)
                         .replace(QLatin1String("$TIME"), time).replace(QLatin1String("$NOW"), now);
        return result;
    };
    v4.sqlite = salesSchema(false);
    v4.postgresql = salesSchema(true);

    Migration v5;
    v5.version = 5;
    v5.description = QStringLiteral("find a payment by its voucher code");
    v5.sqlite = v5.postgresql = QStringList {
        QStringLiteral("CREATE INDEX ventas_pagos_referencia ON ventas_pagos (referencia)"),
    };

    Migration v6;
    v6.version = 6;
    v6.description = QStringLiteral("settings shared by the registers, printed copies of a sale");
    v6.sqlite = v6.postgresql = QStringList {
        QStringLiteral("CREATE TABLE configuracion (clave TEXT PRIMARY KEY, valor TEXT NOT NULL)"),
        QStringLiteral("ALTER TABLE ventas ADD COLUMN impresiones INTEGER NOT NULL DEFAULT 0"),
    };

    Migration v7;
    v7.version = 7;
    v7.description = QStringLiteral("a sale's identity, so that pushing it twice saves it once");
    v7.sqlite = v7.postgresql = QStringList {
        QStringLiteral("ALTER TABLE ventas ADD COLUMN uuid TEXT"),
        QStringLiteral("CREATE UNIQUE INDEX ventas_uuid ON ventas (uuid)"),
    };

    return { v1, v2, v3, v4, v5, v6, v7 };
}
