#ifndef CONNECTIONMONITOR_H
#define CONNECTIONMONITOR_H

#include <QObject>
#include <QSqlDatabase>
#include <QThread>
#include <QTimer>

class QSqlQuery;
class ServerProbe;

/*
    Keeps the database connection alive for the core's services.

    SQLite: a local file, nothing to monitor; exec() is a plain QSqlQuery::exec().

    PostgreSQL (shared server on the LAN): the connection can drop when the
    server restarts or the network fails.
      * exec(): if a statement fails because the connection is gone, reconnect
        and, outside a transaction, run it again on the new connection. Inside
        a transaction it just fails (the transaction died with the old
        connection), with a message saying so.
      * a health check runs on a background thread, on its own connection, so
        a dead or unreachable server never freezes the window. It notices an
        outage and a server restart even when nobody is using the till.
      * notifications from other tills (LISTEN) are also read once a second,
        in case Qt's socket notifier misses them after a reconnect.
      * while the server is unreachable the main connection isn't touched:
        statements fail at once, and the background check keeps trying. When
        the server answers again, the main connection is reopened (quick,
        since the server is known to be up).
    Services listen to reconnected() to restore per-connection state
    (LISTEN subscriptions) and refresh what they show.

    The window can still wait once, up to connect_timeout, when a statement
    is the first thing to discover an outage (the monitor still believed the
    server was up, so it tries to reconnect right away).
*/
class ConnectionMonitor : public QObject
{
    Q_OBJECT
public:
    ConnectionMonitor(const QString &connectionName, bool postgres, QObject *parent = nullptr);
    ~ConnectionMonitor() override;

    QSqlDatabase database() const;
    QString connectionName() const { return m_connectionName; }
    bool isPostgres() const { return m_postgres; }
    bool isConnected() const { return m_connected; }

    /// Run a query (prepared or not). On a lost connection: reconnect, and
    /// retry once unless inTransaction. `query` may be replaced by a new
    /// QSqlQuery on the new connection; read results from it as usual.
    bool exec(QSqlQuery &query, bool inTransaction = false);
    /// Same, for a statement that isn't prepared (e.g. NOTIFY, which
    /// PostgreSQL can't prepare).
    bool exec(QSqlQuery &query, const QString &sql, bool inTransaction = false);

    /// Why the last exec() failed (database error or connection message).
    QString lastError() const { return m_lastError; }

    /// Close and reopen the connection (same settings). Emits reconnected()
    /// or connectionLost(). While the server is known to be unreachable it
    /// returns false at once: the background check reconnects when it's back.
    bool reconnect();

    /// true if the connection is known to be broken (PostgreSQL only)
    bool connectionBroken() const;

    /// Background check period while connected (default 15000; 0 disables).
    /// While disconnected it retries more often (at most every 3 s).
    void setHealthCheckInterval(int ms);

signals:
    void connectionLost(const QString &error);
    void reconnected();

    void probeRequested();          ///< internal: runs ServerProbe::check()

private slots:
    void healthCheck();
    void pollNotifications();
    void onProbeResult(bool reachable, const QString &serverStart, const QString &error);

private:
    bool reopen();
    bool recover(QSqlQuery &query, bool inTransaction);
    void setConnected(bool connected, const QString &error = QString());
    void updateTimer();
    QString readServerStart() const;

    QString m_connectionName;
    QString m_lastError;
    QString m_serverStart;          ///< pg_postmaster_start_time of our connection's server
    bool    m_postgres;
    bool    m_connected = true;
    bool    m_probePending = false;
    int     m_interval = 0;
    QTimer  m_healthTimer;
    QTimer  m_notificationTimer;
    QThread m_probeThread;
    ServerProbe *m_probe = nullptr;
};

/// Lives on ConnectionMonitor's background thread, with its own connection
/// (a clone of the main one), so waiting on the network blocks only it.
class ServerProbe : public QObject
{
    Q_OBJECT
public:
    explicit ServerProbe(const QString &sourceConnection);

public slots:
    void check();
    void shutdown();

signals:
    void result(bool reachable, const QString &serverStart, const QString &error);

private:
    QString m_source;
    QString m_name;
};

#endif // CONNECTIONMONITOR_H
