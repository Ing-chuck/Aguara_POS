#include "sqluserservice.h"
#include "connectionmonitor.h"
#include "errors.h"
#include "logging.h"
#include "passwordhash.h"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QSysInfo>
#include <QVariant>

namespace {
const int kMaxFailures = 5;
const int kLockSeconds = 60;
const int kMinPasswordLength = 6;

QString nowIso(int addSeconds = 0)
{
    return QDateTime::currentDateTime().addSecs(addSeconds).toString(Qt::ISODate);
}
} // namespace

SqlUserService::SqlUserService(ConnectionMonitor *monitor, QObject *parent)
    : QObject(parent), m_monitor(monitor), m_connectionName(monitor->connectionName())
{
}

QSqlDatabase SqlUserService::db() const
{
    return QSqlDatabase::database(m_connectionName, false);
}

bool SqlUserService::exec(QSqlQuery &q) const
{
    if (m_monitor->exec(q, m_inTransaction))
        return true;
    m_lastError = m_monitor->lastError();
    return false;
}

bool SqlUserService::begin()
{
    if (m_monitor->connectionBroken())
        m_monitor->reconnect();
    if (!db().transaction()) {
        m_lastError = AguaraErrors::logAndExplain(db().lastError(), QStringLiteral("BEGIN"));
        return false;
    }
    m_inTransaction = true;
    return true;
}

bool SqlUserService::commit()
{
    m_inTransaction = false;
    QSqlDatabase database = db();
    if (database.commit())
        return true;
    m_lastError = AguaraErrors::logAndExplain(database.lastError(), QStringLiteral("COMMIT"));
    database.rollback();
    return false;
}

void SqlUserService::rollback()
{
    m_inTransaction = false;
    db().rollback();
}

// --- the session ------------------------------------------------------------

bool SqlUserService::hasUsers() const
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM usuarios"));
    return exec(q) && q.next() && q.value(0).toInt() > 0;
}

bool SqlUserService::validPassword(const QString &password)
{
    if (password.size() >= kMinPasswordLength)
        return true;
    m_lastError = tr("La contraseña debe tener al menos %1 caracteres.").arg(kMinPasswordLength);
    return false;
}

bool SqlUserService::createFirstAdmin(const QString &username, const QString &fullName, const QString &password)
{
    if (hasUsers()) {
        m_lastError = tr("Ya hay usuarios creados.");
        return false;
    }
    const QString name = username.trimmed();
    if (name.isEmpty() || fullName.trimmed().isEmpty()) {
        m_lastError = tr("Falta el usuario o el nombre.");
        return false;
    }
    if (!validPassword(password))
        return false;

    if (!begin())
        return false;
    QSqlQuery q(db());
    q.prepare(QStringLiteral("INSERT INTO usuarios (usuario, nombre, clave) VALUES (?, ?, ?)"));
    q.addBindValue(name);
    q.addBindValue(fullName.trimmed());
    q.addBindValue(PasswordHash::create(password));
    if (!exec(q)) {
        rollback();
        return false;
    }
    const qint64 id = q.lastInsertId().toLongLong();
    QSqlQuery r(db());
    r.prepare(QStringLiteral("INSERT INTO usuario_tiene_roles (usuario_id, rol_id)"
                             " SELECT ?, id FROM roles WHERE todos_los_permisos = 1"));
    r.addBindValue(id);
    if (!exec(r)) {
        rollback();
        return false;
    }
    if (!commit())
        return false;
    writeAudit(name, QString(), QStringLiteral("usuario.crear"), QStringLiteral("usuario"),
               QString::number(id), QString(), tr("primer administrador"));
    return login(name, password);
}

/// The password check behind signing in and supervisor authorization,
/// including the wait after too many failures.
bool SqlUserService::checkCredentials(const QString &username, const QString &password, UserAccount *account)
{
    const QString wrong = tr("Usuario o contraseña incorrectos.");

    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT id, clave, es_activo, intentos_fallidos, bloqueado_hasta"
                             " FROM usuarios WHERE lower(usuario) = lower(?)"));
    q.addBindValue(username.trimmed());
    if (!exec(q))
        return false;
    if (!q.next()) {
        // same work as a wrong password: the time doesn't reveal which names exist
        static const QString dummy = PasswordHash::create(QString());
        PasswordHash::verify(password, dummy);
        m_lastError = wrong;
        writeAudit(username.trimmed(), QString(), QStringLiteral("sesion.fallida"), QStringLiteral("usuario"),
                   QString(), QString(), tr("usuario inexistente"));
        return false;
    }
    const qint64 id = q.value(0).toLongLong();
    const QString stored = q.value(1).toString();
    const bool active = q.value(2).toBool();
    const int failures = q.value(3).toInt();
    const QDateTime lockedUntil = QDateTime::fromString(q.value(4).toString(), Qt::ISODate);
    q.finish();

    if (lockedUntil.isValid() && lockedUntil > QDateTime::currentDateTime()) {
        m_lastError = tr("Demasiados intentos fallidos. Espere un minuto e intente de nuevo.");
        return false;
    }

    if (!PasswordHash::verify(password, stored)) {
        const int count = failures + 1;
        QSqlQuery w(db());
        w.prepare(QStringLiteral("UPDATE usuarios SET intentos_fallidos = ?, bloqueado_hasta = ? WHERE id = ?"));
        w.addBindValue(count >= kMaxFailures ? 0 : count);
        w.addBindValue(count >= kMaxFailures ? QVariant(nowIso(kLockSeconds)) : QVariant(QString()));
        w.addBindValue(id);
        exec(w);
        m_lastError = wrong;
        writeAudit(username.trimmed(), QString(), QStringLiteral("sesion.fallida"), QStringLiteral("usuario"),
                   QString::number(id), QString(), tr("contraseña incorrecta"));
        return false;
    }

    if (!active) {
        m_lastError = tr("Este usuario está desactivado.");
        return false;
    }

    QSqlQuery w(db());
    if (PasswordHash::needsUpgrade(stored)) {       // the password is at hand: raise the cost
        w.prepare(QStringLiteral("UPDATE usuarios SET intentos_fallidos = 0, bloqueado_hasta = NULL, clave = ? WHERE id = ?"));
        w.addBindValue(PasswordHash::create(password));
    } else {
        w.prepare(QStringLiteral("UPDATE usuarios SET intentos_fallidos = 0, bloqueado_hasta = NULL WHERE id = ?"));
    }
    w.addBindValue(id);
    exec(w);

    const std::optional<UserAccount> loaded = loadUser(id);
    if (!loaded)
        return false;
    *account = *loaded;
    return true;
}

bool SqlUserService::login(const QString &username, const QString &password)
{
    UserAccount account;
    if (!checkCredentials(username, password, &account))
        return false;
    bool all = false;
    QSet<QString> codes;
    if (!loadPermissions(account.id, &all, &codes))
        return false;

    const bool sameUser = m_current && m_current->id == account.id;
    if (m_current && !sameUser)
        logout();
    m_current = account;
    m_allPermissions = all;
    m_permissions = codes;
    if (!sameUser)
        audit(QStringLiteral("sesion.ingreso"), QStringLiteral("usuario"), QString::number(account.id));
    return true;
}

void SqlUserService::logout()
{
    if (!m_current)
        return;
    audit(QStringLiteral("sesion.salida"), QStringLiteral("usuario"), QString::number(m_current->id));
    m_current.reset();
    m_allPermissions = false;
    m_permissions.clear();
}

bool SqlUserService::can(const QString &permission) const
{
    return m_current && (m_allPermissions || m_permissions.contains(permission));
}

bool SqlUserService::authorize(const QString &permission, const QString &reason, QWidget *parent)
{
    if (can(permission))
        return true;
    if (!m_prompt) {
        m_lastError = tr("Esta acción requiere autorización.");
        return false;
    }

    QString description = permission;
    for (const PermissionInfo &p : permissions()) {
        if (p.code == permission)
            description = p.description;
    }

    QString error;
    for (;;) {
        QString username, password;
        if (!m_prompt(description, reason, error, parent, &username, &password)) {
            m_lastError = tr("No se autorizó la acción.");
            return false;
        }
        UserAccount supervisor;
        if (!checkCredentials(username, password, &supervisor)) {
            error = m_lastError;
            continue;
        }
        bool all = false;
        QSet<QString> codes;
        if (!loadPermissions(supervisor.id, &all, &codes) || !(all || codes.contains(permission))) {
            error = tr("%1 no tiene permiso para autorizar esto.").arg(supervisor.fullName);
            continue;
        }
        writeAudit(m_current ? m_current->username : QString(), supervisor.username,
                   QStringLiteral("autorizacion"), QStringLiteral("permiso"), permission, QString(), reason);
        return true;
    }
}

// --- the audit log ----------------------------------------------------------

void SqlUserService::writeAudit(const QString &userName, const QString &authorizedBy, const QString &action,
                                const QString &entity, const QString &entityId,
                                const QString &before, const QString &after)
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("INSERT INTO auditoria (usuario_id, usuario, autorizado_por, puesto, accion,"
                             " entidad, entidad_id, antes, despues) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(m_current ? QVariant(m_current->id) : QVariant(QVariant::LongLong));
    q.addBindValue(userName);
    q.addBindValue(authorizedBy);
    q.addBindValue(QSysInfo::machineHostName());
    q.addBindValue(action);
    q.addBindValue(entity);
    q.addBindValue(entityId);
    q.addBindValue(before);
    q.addBindValue(after);
    QString keep = m_lastError;         // the caller's error, not ours
    if (!exec(q))
        qCWarning(lcDatabase) << "Could not write the audit record" << action << entityId;
    m_lastError = keep;
}

void SqlUserService::audit(const QString &action, const QString &entity, const QString &entityId,
                           const QString &before, const QString &after)
{
    writeAudit(m_current ? m_current->username : QString(), QString(), action, entity, entityId, before, after);
}

QList<AuditEntry> SqlUserService::auditEntries(const AuditFilter &filter) const
{
    QString sql = QStringLiteral("SELECT id, fecha, usuario, autorizado_por, puesto, accion, entidad, entidad_id,"
                                 " antes, despues FROM auditoria WHERE 1 = 1");
    QVariantList values;
    if (filter.from.isValid()) {
        sql += QStringLiteral(" AND fecha >= ?");
        values << filter.from.toString(Qt::ISODate);
    }
    if (filter.to.isValid()) {
        sql += QStringLiteral(" AND fecha < ?");
        values << filter.to.addDays(1).toString(Qt::ISODate);
    }
    if (!filter.text.trimmed().isEmpty()) {
        sql += QStringLiteral(" AND (lower(usuario) LIKE ? OR lower(autorizado_por) LIKE ? OR lower(accion) LIKE ?"
                              " OR lower(entidad_id) LIKE ? OR lower(antes) LIKE ? OR lower(despues) LIKE ?)");
        const QString like = QLatin1Char('%') + filter.text.trimmed().toLower() + QLatin1Char('%');
        for (int i = 0; i < 6; ++i)
            values << like;
    }
    sql += QStringLiteral(" ORDER BY id DESC LIMIT %1").arg(qMax(filter.limit, 1));

    QList<AuditEntry> result;
    QSqlQuery q(db());
    q.prepare(sql);
    for (const QVariant &v : qAsConst(values))
        q.addBindValue(v);
    if (!exec(q))
        return result;
    while (q.next()) {
        AuditEntry e;
        e.id = q.value(0).toLongLong();
        e.when = q.value(1).toDateTime();
        e.user = q.value(2).toString();
        e.authorizedBy = q.value(3).toString();
        e.station = q.value(4).toString();
        e.action = q.value(5).toString();
        e.entity = q.value(6).toString();
        e.entityId = q.value(7).toString();
        e.before = q.value(8).toString();
        e.after = q.value(9).toString();
        result << e;
    }
    return result;
}

// --- users ------------------------------------------------------------------

std::optional<UserAccount> SqlUserService::loadUser(qint64 id) const
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT id, usuario, nombre, es_activo, debe_cambiar_clave FROM usuarios WHERE id = ?"));
    q.addBindValue(id);
    if (!exec(q) || !q.next())
        return std::nullopt;
    UserAccount u;
    u.id = q.value(0).toLongLong();
    u.username = q.value(1).toString();
    u.fullName = q.value(2).toString();
    u.active = q.value(3).toBool();
    u.mustChangePassword = q.value(4).toBool();

    QSqlQuery r(db());
    r.prepare(QStringLiteral("SELECT r.id, r.nombre FROM usuario_tiene_roles ur JOIN roles r ON r.id = ur.rol_id"
                             " WHERE ur.usuario_id = ? ORDER BY lower(r.nombre)"));
    r.addBindValue(id);
    if (exec(r)) {
        while (r.next()) {
            u.roleIds << r.value(0).toLongLong();
            u.roleNames << r.value(1).toString();
        }
    }
    return u;
}

QList<UserAccount> SqlUserService::users() const
{
    QList<UserAccount> result;
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT id FROM usuarios ORDER BY lower(nombre)"));
    if (!exec(q))
        return result;
    QList<qint64> ids;
    while (q.next())
        ids << q.value(0).toLongLong();
    for (qint64 id : qAsConst(ids)) {
        if (const auto u = loadUser(id))
            result << *u;
    }
    return result;
}

bool SqlUserService::writeUserRoles(qint64 userId, const QList<qint64> &roleIds)
{
    QSqlQuery d(db());
    d.prepare(QStringLiteral("DELETE FROM usuario_tiene_roles WHERE usuario_id = ?"));
    d.addBindValue(userId);
    if (!exec(d))
        return false;
    for (qint64 role : roleIds) {
        QSqlQuery i(db());
        i.prepare(QStringLiteral("INSERT INTO usuario_tiene_roles (usuario_id, rol_id) VALUES (?, ?)"));
        i.addBindValue(userId);
        i.addBindValue(role);
        if (!exec(i))
            return false;
    }
    return true;
}

bool SqlUserService::hasActiveAdmin() const
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM usuarios u JOIN usuario_tiene_roles ur ON ur.usuario_id = u.id"
                             " JOIN roles r ON r.id = ur.rol_id"
                             " WHERE u.es_activo = 1 AND r.todos_los_permisos = 1"));
    return exec(q) && q.next() && q.value(0).toInt() > 0;
}

/// The screens check first; this keeps the rule true for any caller.
bool SqlUserService::mayAdminister()
{
    if (can(QStringLiteral("usuarios.administrar")))
        return true;
    m_lastError = tr("No tiene permiso para administrar usuarios.");
    return false;
}

bool SqlUserService::saveUser(const UserAccount &user, const QString &password)
{
    if (!mayAdminister())
        return false;
    const QString username = user.username.trimmed();
    const QString fullName = user.fullName.trimmed();
    if (username.isEmpty() || fullName.isEmpty()) {
        m_lastError = tr("Falta el usuario o el nombre.");
        return false;
    }
    if (user.roleIds.isEmpty()) {
        m_lastError = tr("Asigne al menos un rol.");
        return false;
    }
    if (user.id == 0 && password.isEmpty()) {
        m_lastError = tr("Falta la contraseña.");
        return false;
    }
    if (!password.isEmpty() && !validPassword(password))
        return false;
    if (m_current && m_current->id == user.id && !user.active) {
        m_lastError = tr("No puede desactivar su propio usuario.");
        return false;
    }

    QSqlQuery taken(db());
    taken.prepare(QStringLiteral("SELECT id FROM usuarios WHERE lower(usuario) = lower(?) AND id <> ?"));
    taken.addBindValue(username);
    taken.addBindValue(user.id);
    if (!exec(taken))
        return false;
    if (taken.next()) {
        m_lastError = tr("Ya existe el usuario %1.").arg(username);
        return false;
    }

    const std::optional<UserAccount> previous = user.id ? loadUser(user.id) : std::nullopt;
    if (user.id && !previous) {
        m_lastError = tr("El usuario ya no existe.");
        return false;
    }

    if (!begin())
        return false;
    qint64 id = user.id;
    QSqlQuery w(db());
    if (id == 0) {
        w.prepare(QStringLiteral("INSERT INTO usuarios (usuario, nombre, clave, es_activo, debe_cambiar_clave)"
                                 " VALUES (?, ?, ?, ?, 1)"));
        w.addBindValue(username);
        w.addBindValue(fullName);
        w.addBindValue(PasswordHash::create(password));
        w.addBindValue(user.active ? 1 : 0);
    } else if (password.isEmpty()) {
        w.prepare(QStringLiteral("UPDATE usuarios SET usuario = ?, nombre = ?, es_activo = ? WHERE id = ?"));
        w.addBindValue(username);
        w.addBindValue(fullName);
        w.addBindValue(user.active ? 1 : 0);
        w.addBindValue(id);
    } else {
        w.prepare(QStringLiteral("UPDATE usuarios SET usuario = ?, nombre = ?, es_activo = ?, clave = ?,"
                                 " debe_cambiar_clave = 1, intentos_fallidos = 0, bloqueado_hasta = NULL WHERE id = ?"));
        w.addBindValue(username);
        w.addBindValue(fullName);
        w.addBindValue(user.active ? 1 : 0);
        w.addBindValue(PasswordHash::create(password));
        w.addBindValue(id);
    }
    if (!exec(w)) {
        rollback();
        return false;
    }
    if (id == 0)
        id = w.lastInsertId().toLongLong();

    if (!writeUserRoles(id, user.roleIds)) {
        rollback();
        return false;
    }
    if (!hasActiveAdmin()) {
        rollback();
        m_lastError = tr("Debe quedar al menos un administrador activo.");
        return false;
    }
    if (!commit())
        return false;

    const auto describe = [](const UserAccount &u) {
        return QStringLiteral("%1 (%2); roles: %3; %4").arg(u.username, u.fullName, u.roleNames.join(QStringLiteral(", ")),
                                                           u.active ? QStringLiteral("activo") : QStringLiteral("inactivo"));
    };
    const std::optional<UserAccount> saved = loadUser(id);
    audit(user.id ? QStringLiteral("usuario.modificar") : QStringLiteral("usuario.crear"), QStringLiteral("usuario"),
          QString::number(id), previous ? describe(*previous) : QString(), saved ? describe(*saved) : QString());
    if (user.id && !password.isEmpty())
        audit(QStringLiteral("usuario.clave"), QStringLiteral("usuario"), QString::number(id),
              QString(), tr("contraseña restablecida"));

    if (m_current && m_current->id == id && saved) {    // editing yourself
        m_current = *saved;
        loadPermissions(id, &m_allPermissions, &m_permissions);
    }
    return true;
}

bool SqlUserService::changePassword(const QString &current, const QString &replacement)
{
    if (!m_current) {
        m_lastError = tr("No hay una sesión iniciada.");
        return false;
    }
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT clave FROM usuarios WHERE id = ?"));
    q.addBindValue(m_current->id);
    if (!exec(q))
        return false;
    if (!q.next() || !PasswordHash::verify(current, q.value(0).toString())) {
        m_lastError = tr("La contraseña actual no es correcta.");
        return false;
    }
    q.finish();
    if (!validPassword(replacement))
        return false;
    if (replacement == current) {
        m_lastError = tr("La contraseña nueva debe ser distinta de la actual.");
        return false;
    }
    QSqlQuery w(db());
    w.prepare(QStringLiteral("UPDATE usuarios SET clave = ?, debe_cambiar_clave = 0 WHERE id = ?"));
    w.addBindValue(PasswordHash::create(replacement));
    w.addBindValue(m_current->id);
    if (!exec(w))
        return false;
    m_current->mustChangePassword = false;
    audit(QStringLiteral("usuario.clave"), QStringLiteral("usuario"), QString::number(m_current->id),
          QString(), tr("cambió su contraseña"));
    return true;
}

// --- roles and permissions --------------------------------------------------

bool SqlUserService::loadPermissions(qint64 userId, bool *all, QSet<QString> *codes) const
{
    *all = false;
    codes->clear();
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM usuario_tiene_roles ur JOIN roles r ON r.id = ur.rol_id"
                             " WHERE ur.usuario_id = ? AND r.todos_los_permisos = 1"));
    q.addBindValue(userId);
    if (!exec(q) || !q.next())
        return false;
    *all = q.value(0).toInt() > 0;

    QSqlQuery p(db());
    p.prepare(QStringLiteral("SELECT DISTINCT rp.permiso FROM usuario_tiene_roles ur"
                             " JOIN rol_tiene_permisos rp ON rp.rol_id = ur.rol_id WHERE ur.usuario_id = ?"));
    p.addBindValue(userId);
    if (!exec(p))
        return false;
    while (p.next())
        codes->insert(p.value(0).toString());
    return true;
}

bool SqlUserService::registerPermissions(const QList<PermissionInfo> &list)
{
    if (!begin())
        return false;
    for (const PermissionInfo &p : list) {
        QSqlQuery q(db());
        q.prepare(QStringLiteral("SELECT 1 FROM permisos WHERE codigo = ?"));
        q.addBindValue(p.code);
        if (!exec(q)) {
            rollback();
            return false;
        }
        const bool known = q.next();
        q.finish();

        QSqlQuery w(db());
        if (known) {
            w.prepare(QStringLiteral("UPDATE permisos SET descripcion = ?, grupo = ? WHERE codigo = ?"));
            w.addBindValue(p.description);
            w.addBindValue(p.group);
            w.addBindValue(p.code);
        } else {
            w.prepare(QStringLiteral("INSERT INTO permisos (descripcion, grupo, codigo) VALUES (?, ?, ?)"));
            w.addBindValue(p.description);
            w.addBindValue(p.group);
            w.addBindValue(p.code);
        }
        if (!exec(w)) {
            rollback();
            return false;
        }
        if (known)
            continue;       // grants belong to the administrator from now on

        QStringList grantTo;
        if (p.forManagers)
            grantTo << QStringLiteral("Encargado");
        if (p.forCashiers)
            grantTo << QStringLiteral("Cajero");
        for (const QString &role : qAsConst(grantTo)) {
            QSqlQuery g(db());
            g.prepare(QStringLiteral("INSERT INTO rol_tiene_permisos (rol_id, permiso)"
                                     " SELECT id, ? FROM roles WHERE nombre = ? AND es_sistema = 1"));
            g.addBindValue(p.code);
            g.addBindValue(role);
            if (!exec(g)) {
                rollback();
                return false;
            }
        }
    }
    if (!commit())
        return false;
    if (m_current)
        loadPermissions(m_current->id, &m_allPermissions, &m_permissions);
    return true;
}

QList<PermissionInfo> SqlUserService::permissions() const
{
    QList<PermissionInfo> result;
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT codigo, descripcion, grupo FROM permisos ORDER BY lower(grupo), lower(descripcion)"));
    if (!exec(q))
        return result;
    while (q.next()) {
        PermissionInfo p;
        p.code = q.value(0).toString();
        p.description = q.value(1).toString();
        p.group = q.value(2).toString();
        result << p;
    }
    return result;
}

QList<UserRole> SqlUserService::roles() const
{
    QList<UserRole> result;
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT id, nombre, descripcion, todos_los_permisos, es_sistema FROM roles ORDER BY id"));
    if (!exec(q))
        return result;
    while (q.next()) {
        UserRole r;
        r.id = q.value(0).toLongLong();
        r.name = q.value(1).toString();
        r.description = q.value(2).toString();
        r.allPermissions = q.value(3).toBool();
        r.system = q.value(4).toBool();
        result << r;
    }
    for (UserRole &r : result) {
        QSqlQuery p(db());
        p.prepare(QStringLiteral("SELECT permiso FROM rol_tiene_permisos WHERE rol_id = ? ORDER BY permiso"));
        p.addBindValue(r.id);
        if (exec(p)) {
            while (p.next())
                r.permissions << p.value(0).toString();
        }
    }
    return result;
}

bool SqlUserService::saveRole(const UserRole &role)
{
    if (!mayAdminister())
        return false;
    const QString name = role.name.trimmed();
    if (name.isEmpty()) {
        m_lastError = tr("Falta el nombre del rol.");
        return false;
    }

    UserRole previous;
    for (const UserRole &r : roles()) {
        if (r.id == role.id)
            previous = r;
        else if (r.name.compare(name, Qt::CaseInsensitive) == 0) {
            m_lastError = tr("Ya existe el rol %1.").arg(name);
            return false;
        }
    }
    if (role.id && previous.allPermissions) {
        m_lastError = tr("El rol %1 tiene todos los permisos y no se puede modificar.").arg(previous.name);
        return false;
    }
    if (previous.system && previous.name != name) {
        m_lastError = tr("Los roles del sistema no se pueden renombrar.");   // grants find them by name
        return false;
    }

    if (!begin())
        return false;
    qint64 id = role.id;
    QSqlQuery w(db());
    if (id == 0) {
        w.prepare(QStringLiteral("INSERT INTO roles (nombre, descripcion) VALUES (?, ?)"));
        w.addBindValue(name);
        w.addBindValue(role.description.trimmed());
    } else {
        w.prepare(QStringLiteral("UPDATE roles SET nombre = ?, descripcion = ? WHERE id = ?"));
        w.addBindValue(name);
        w.addBindValue(role.description.trimmed());
        w.addBindValue(id);
    }
    if (!exec(w)) {
        rollback();
        return false;
    }
    if (id == 0)
        id = w.lastInsertId().toLongLong();

    QSqlQuery d(db());
    d.prepare(QStringLiteral("DELETE FROM rol_tiene_permisos WHERE rol_id = ?"));
    d.addBindValue(id);
    if (!exec(d)) {
        rollback();
        return false;
    }
    for (const QString &code : role.permissions) {
        QSqlQuery i(db());
        i.prepare(QStringLiteral("INSERT INTO rol_tiene_permisos (rol_id, permiso) VALUES (?, ?)"));
        i.addBindValue(id);
        i.addBindValue(code);
        if (!exec(i)) {
            rollback();
            return false;
        }
    }
    if (!commit())
        return false;

    audit(role.id ? QStringLiteral("rol.modificar") : QStringLiteral("rol.crear"), QStringLiteral("rol"),
          QString::number(id), previous.permissions.join(QStringLiteral(", ")), role.permissions.join(QStringLiteral(", ")));
    if (m_current)
        loadPermissions(m_current->id, &m_allPermissions, &m_permissions);
    return true;
}

bool SqlUserService::removeRole(qint64 id)
{
    if (!mayAdminister())
        return false;
    UserRole role;
    for (const UserRole &r : roles()) {
        if (r.id == id)
            role = r;
    }
    if (role.id == 0) {
        m_lastError = tr("El rol ya no existe.");
        return false;
    }
    if (role.system) {
        m_lastError = tr("Los roles del sistema no se pueden quitar.");
        return false;
    }
    QSqlQuery u(db());
    u.prepare(QStringLiteral("SELECT COUNT(*) FROM usuario_tiene_roles WHERE rol_id = ?"));
    u.addBindValue(id);
    if (!exec(u) || !u.next())
        return false;
    if (u.value(0).toInt() > 0) {
        m_lastError = tr("Hay usuarios con el rol %1: cámbielos primero.").arg(role.name);
        return false;
    }
    u.finish();
    QSqlQuery d(db());
    d.prepare(QStringLiteral("DELETE FROM roles WHERE id = ?"));
    d.addBindValue(id);
    if (!exec(d))
        return false;
    audit(QStringLiteral("rol.quitar"), QStringLiteral("rol"), QString::number(id), role.name);
    return true;
}
