#ifndef SQLCASHSERVICE_H
#define SQLCASHSERVICE_H

#include <cashservice.h>

#include <QObject>
#include <QSqlDatabase>

class ConnectionMonitor;
class QSqlQuery;
class UserService;

/// CashService on the core's database.
class SqlCashService : public QObject, public CashService
{
    Q_OBJECT
    Q_INTERFACES(CashService)

public:
    /// `station` identifies this computer's register (default: the host name).
    explicit SqlCashService(ConnectionMonitor *monitor, const QString &station = QString(), QObject *parent = nullptr);

    /// Who is acting, and where changes are recorded (may stay null).
    void setUsers(UserService *users) { m_users = users; }

    CashRegister currentRegister() override;
    bool renameRegister(const QString &name) override;
    std::optional<CashSession> currentSession() override;
    bool openSession(const CashCount &count) override;
    bool addMovement(const char *type, Money amount, const QString &reason) override;
    QList<CashMovement> movements(qint64 sessionId) const override;
    CloseResult closeSession(const CashCount &count) override;
    bool closeWithDifference(const CashCount &count, const QString &reason, const QString &authorizedBy) override;
    QList<CashSession> sessions(int limit = 200) const override;
    std::optional<CashSessionReport> sessionReport(qint64 sessionId) const override;
    QString lastError() const override { return m_lastError; }

private:
    QSqlDatabase db() const;
    bool exec(QSqlQuery &q) const;
    bool begin();
    bool commit();
    void rollback();
    void audit(const QString &action, const QString &entityId, const QString &after);
    QString userName() const;

    QList<CashSession> readSessions(const QString &where, const QVariantList &values) const;
    Money expectedCash(qint64 sessionId, Money openingFloat) const;
    CashCount readCount(qint64 sessionId, const char *moment) const;
    bool writeCount(qint64 sessionId, const char *moment, const CashCount &count);
    bool finish(const CashSession &session, const CashCount &count, Money expected,
                const QString &reason, const QString &authorizedBy);

    ConnectionMonitor *m_monitor;
    QString m_connectionName;
    QString m_station;
    UserService *m_users = nullptr;
    bool m_inTransaction = false;
    mutable QString m_lastError;
};

#endif // SQLCASHSERVICE_H
