#ifndef USERSERVICE_H
#define USERSERVICE_H

#include <QDate>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <QtPlugin>

#include <optional>

class QWidget;

/// One thing a user may be allowed to do. Modules declare theirs with
/// PermissionProvider (interfaces.h). `code` is stable ("inventario.baja");
/// the flags say who gets it the first time it is registered: afterwards the
/// roles are the administrator's to change.
struct PermissionInfo {
    QString code;
    QString description;        ///< shown to the user ("Dar de baja artículos")
    QString group;              ///< heading in the roles screen ("Inventario")
    bool    forManagers = true;     ///< granted to the role Encargado
    bool    forCashiers = false;    ///< granted to the role Cajero
};

/// The permission that opens a module's page. Modules with a page get it
/// automatically (Encargado yes, Cajero no); declare it yourself to change that.
inline QString modulePermission(const QString &moduleId)
{
    return QStringLiteral("modulo.") + moduleId;
}

struct UserAccount {
    qint64  id = 0;
    QString username;
    QString fullName;
    bool    active = true;
    bool    mustChangePassword = false;
    QList<qint64> roleIds;
    QStringList   roleNames;    ///< for display
};

struct UserRole {
    qint64  id = 0;
    QString name;
    QString description;
    bool    allPermissions = false; ///< Administrador: everything, present and future
    bool    system = false;         ///< created by the program: can't be removed
    QStringList permissions;        ///< codes
};

struct AuditEntry {
    qint64    id = 0;
    QDateTime when;
    QString   user;
    QString   authorizedBy;
    QString   station;
    QString   action;       ///< "producto.modificar"
    QString   entity;       ///< "producto"
    QString   entityId;
    QString   before;
    QString   after;
};

struct AuditFilter {
    QDate   from;           ///< invalid = no limit
    QDate   to;             ///< inclusive; invalid = no limit
    QString text;           ///< in the user, action, entity or values
    int     limit = 500;    ///< newest first
};

/// Who is using the program and what they may do. Provided by the core.
class UserService {
public:
    virtual ~UserService() = default;

    // --- the session ----------------------------------------------------
    virtual std::optional<UserAccount> currentUser() const = 0;
    /// May the signed-in user do this? Use it to hide or disable things.
    virtual bool can(const QString &permission) const = 0;
    /// Check before a sensitive action. true if the signed-in user may do
    /// it; otherwise a supervisor is asked for their credentials on top of
    /// `parent`, without closing the session. `reason` says what is being
    /// authorized ("Dar de baja LECHE ENTERA 1L"). Recorded in the audit log.
    virtual bool authorize(const QString &permission, const QString &reason, QWidget *parent) = 0;

    // --- the audit log --------------------------------------------------
    /// Record what the signed-in user just did.
    virtual void audit(const QString &action, const QString &entity, const QString &entityId,
                       const QString &before = QString(), const QString &after = QString()) = 0;
    virtual QList<AuditEntry> auditEntries(const AuditFilter &filter = AuditFilter()) const = 0;

    // --- administration (permission usuarios.administrar) ---------------
    virtual QList<UserAccount> users() const = 0;
    /// Create (id == 0, `password` required) or update. A non-empty password
    /// replaces the old one and the user must change it at the next login.
    virtual bool saveUser(const UserAccount &user, const QString &password = QString()) = 0;
    virtual QList<UserRole> roles() const = 0;
    virtual bool saveRole(const UserRole &role) = 0;
    virtual bool removeRole(qint64 id) = 0;
    virtual QList<PermissionInfo> permissions() const = 0;

    /// The signed-in user changes their own password.
    virtual bool changePassword(const QString &current, const QString &replacement) = 0;

    /// Why the last call failed, in Spanish for the user.
    virtual QString lastError() const = 0;
};

#define UserService_iid "org.AguaraPOS.UserService/1.0"
Q_DECLARE_INTERFACE(UserService, UserService_iid)

#endif // USERSERVICE_H
