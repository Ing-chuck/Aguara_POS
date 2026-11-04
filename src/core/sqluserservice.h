#ifndef SQLUSERSERVICE_H
#define SQLUSERSERVICE_H

#include <userservice.h>

#include <QObject>
#include <QSet>
#include <QSqlDatabase>

#include <functional>

class ConnectionMonitor;
class QSqlQuery;

/*
    UserService on the core's database, plus what only the core may do:
    the first administrator, signing in and out, and registering permissions.

    Failed sign-ins: after kMaxFailures in a row the account waits a minute.
*/
class SqlUserService : public QObject, public UserService
{
    Q_OBJECT
    Q_INTERFACES(UserService)

public:
    /// Shows the supervisor dialog: fills in `username` and `password`, shows
    /// `error` if not empty, and returns false when cancelled. Replaceable
    /// so the tests don't need a screen.
    using SupervisorPrompt = std::function<bool(const QString &permissionDescription, const QString &reason,
                                                const QString &error, QWidget *parent,
                                                QString *username, QString *password)>;

    /// `monitor` provides the connection and must outlive the service.
    explicit SqlUserService(ConnectionMonitor *monitor, QObject *parent = nullptr);

    void setSupervisorPrompt(const SupervisorPrompt &prompt) { m_prompt = prompt; }

    // --- only the core --------------------------------------------------
    bool hasUsers() const;
    /// First run: create the administrator and sign in.
    bool createFirstAdmin(const QString &username, const QString &fullName, const QString &password);
    bool login(const QString &username, const QString &password);
    void logout();
    /// Add permissions not seen before and grant them per their flags.
    /// Reloads what the signed-in user may do.
    bool registerPermissions(const QList<PermissionInfo> &permissions);

    // --- UserService ----------------------------------------------------
    std::optional<UserAccount> currentUser() const override { return m_current; }
    bool can(const QString &permission) const override;
    bool authorize(const QString &permission, const QString &reason, QWidget *parent) override;
    void audit(const QString &action, const QString &entity, const QString &entityId,
               const QString &before = QString(), const QString &after = QString()) override;
    QList<AuditEntry> auditEntries(const AuditFilter &filter = AuditFilter()) const override;
    QList<UserAccount> users() const override;
    bool saveUser(const UserAccount &user, const QString &password = QString()) override;
    QList<UserRole> roles() const override;
    bool saveRole(const UserRole &role) override;
    bool removeRole(qint64 id) override;
    QList<PermissionInfo> permissions() const override;
    bool changePassword(const QString &current, const QString &replacement) override;
    QString lastError() const override { return m_lastError; }

private:
    QSqlDatabase db() const;
    bool exec(QSqlQuery &q) const;
    bool begin();
    bool commit();
    void rollback();

    bool checkCredentials(const QString &username, const QString &password, UserAccount *account);
    std::optional<UserAccount> loadUser(qint64 id) const;
    bool loadPermissions(qint64 userId, bool *all, QSet<QString> *codes) const;
    bool writeUserRoles(qint64 userId, const QList<qint64> &roleIds);
    bool hasActiveAdmin() const;
    bool validPassword(const QString &password);
    bool mayAdminister();
    void writeAudit(const QString &userName, const QString &authorizedBy, const QString &action,
                    const QString &entity, const QString &entityId,
                    const QString &before, const QString &after);

    ConnectionMonitor *m_monitor;
    QString m_connectionName;
    mutable QString m_lastError;
    bool m_inTransaction = false;
    std::optional<UserAccount> m_current;
    bool m_allPermissions = false;
    QSet<QString> m_permissions;
    SupervisorPrompt m_prompt;
};

#endif // SQLUSERSERVICE_H
