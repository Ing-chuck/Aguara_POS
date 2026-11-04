#ifndef SQLCASHSERVICE_H
#define SQLCASHSERVICE_H

#include <cashservice.h>

#include <QObject>
#include <QSqlDatabase>

#include <functional>
#include <optional>

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

    /// How many sales are still on their way: they count in the expected cash,
    /// so a session can't close before they arrive.
    void setPendingSales(const std::function<int()> &pending) { m_pendingSales = pending; }

    /// The last number of this register that this computer knows of.
    qint64 lastNumber() const { return m_lastNumber; }
    void noteNumber(qint64 number) { m_lastNumber = qMax(m_lastNumber, number); }

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
    bool salesArePending();

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
    std::function<int()> m_pendingSales;
    // The last answers from the server: during an outage the till goes on
    // with them, to finish the sale in progress.
    CashRegister m_knownRegister;
    std::optional<CashSession> m_knownSession;
    qint64 m_lastNumber = 0;
    mutable QString m_lastError;
};

#endif // SQLCASHSERVICE_H
