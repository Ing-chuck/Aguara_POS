#include "connectionmonitor.h"

#include <QLibrary>
#include <QSqlDriver>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace {

// libpq's PQstatus(), looked up at runtime: Qt's qsqlpsql driver has already
// loaded libpq.dll, so this needs no PostgreSQL headers or import library.
using PQstatusFn = int (*)(const void *conn);
const int CONNECTION_OK = 0;

PQstatusFn pqStatus()
{
    static PQstatusFn fn = reinterpret_cast<PQstatusFn>(QLibrary::resolve(QStringLiteral("libpq"), "PQstatus"));
    return fn;
}

/// The PGconn* behind a QPSQL connection, or nullptr.
const void *pgConnection(const QSqlDatabase &db)
{
    if (!db.isValid() || !db.driver())
        return nullptr;
    const QVariant handle = db.driver()->handle();
    if (!handle.isValid() || qstrcmp(handle.typeName(), "PGconn*") != 0)
        return nullptr;
    return *static_cast<void *const *>(handle.data());
}

// Changes when the server restarts: tells the background check that the
// main connection died with the old server process, even if nothing used it.
const char kServerStartSql[] = "SELECT pg_postmaster_start_time()::text";

const int kMaxRetryWhileDown = 3000;    // ms between attempts while the server is down
const int kNotificationPoll = 1000;     // ms; see pollNotifications()

} // namespace

// --- ConnectionMonitor ------------------------------------------------------

ConnectionMonitor::ConnectionMonitor(const QString &connectionName, bool postgres, QObject *parent)
    : QObject(parent), m_connectionName(connectionName), m_postgres(postgres)
{
    if (!m_postgres)
        return;

    m_serverStart = readServerStart();

    m_probe = new ServerProbe(m_connectionName);
    m_probe->moveToThread(&m_probeThread);
    connect(this, &ConnectionMonitor::probeRequested, m_probe, &ServerProbe::check);
    connect(m_probe, &ServerProbe::result, this, &ConnectionMonitor::onProbeResult);
    m_probeThread.start();

    connect(&m_healthTimer, &QTimer::timeout, this, &ConnectionMonitor::healthCheck);
    setHealthCheckInterval(15000);

    connect(&m_notificationTimer, &QTimer::timeout, this, &ConnectionMonitor::pollNotifications);
    m_notificationTimer.start(kNotificationPoll);
}

ConnectionMonitor::~ConnectionMonitor()
{
    if (!m_probe)
        return;
    m_healthTimer.stop();
    // close the probe's connection on its own thread (waits for a check in
    // progress, at most connect_timeout)
    QMetaObject::invokeMethod(m_probe, "shutdown", Qt::BlockingQueuedConnection);
    m_probeThread.quit();
    m_probeThread.wait();
    delete m_probe;
}

QSqlDatabase ConnectionMonitor::database() const
{
    return QSqlDatabase::database(m_connectionName, false);   // never reopens by itself: that waits on the network
}

void ConnectionMonitor::setHealthCheckInterval(int ms)
{
    m_interval = m_postgres ? ms : 0;
    updateTimer();
}

void ConnectionMonitor::updateTimer()
{
    if (m_interval <= 0) {
        m_healthTimer.stop();
        return;
    }
    m_healthTimer.start(m_connected ? m_interval : qMin(m_interval, kMaxRetryWhileDown));
}

bool ConnectionMonitor::connectionBroken() const
{
    if (!m_postgres)
        return false;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
    if (!db.isOpen())
        return true;

    if (pqStatus() && db.driver()->handle().isValid())
        return pgConnection(db) == nullptr || pqStatus()(pgConnection(db)) != CONNECTION_OK;
    // libpq not resolvable (shouldn't happen): fall back to the error type
    return db.lastError().type() == QSqlError::ConnectionError;
}

bool ConnectionMonitor::reconnect()
{
    // Known outage: don't make the window wait on a server that isn't there.
    // The background check calls reopen() as soon as it answers again.
    if (!m_connected)
        return false;
    return reopen();
}

bool ConnectionMonitor::reopen()
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
    db.close();                     // keeps host/port/user/password/options
    if (db.open()) {
        m_serverStart = readServerStart();
        setConnected(true);
        emit reconnected();
        return true;
    }
    setConnected(false, db.lastError().text());
    return false;
}

QString ConnectionMonitor::readServerStart() const
{
    QSqlQuery q(database());
    if (q.exec(QString::fromLatin1(kServerStartSql)) && q.next())
        return q.value(0).toString();
    return QString();
}

void ConnectionMonitor::setConnected(bool connected, const QString &error)
{
    if (connected == m_connected)
        return;
    m_connected = connected;
    updateTimer();                  // retry faster while down
    if (!connected)
        emit connectionLost(error);
}

bool ConnectionMonitor::exec(QSqlQuery &query, bool inTransaction)
{
    if (query.exec())
        return true;
    return recover(query, inTransaction);
}

bool ConnectionMonitor::exec(QSqlQuery &query, const QString &sql, bool inTransaction)
{
    if (query.exec(sql))
        return true;
    return recover(query, inTransaction);
}

/// After a failed statement: if the connection is gone, reconnect and (outside
/// a transaction) run the statement again.
bool ConnectionMonitor::recover(QSqlQuery &query, bool inTransaction)
{
    m_lastError = query.lastError().text();
    if (!connectionBroken())
        return false;               // an ordinary error (constraint, syntax...)

    // The connection is gone. Remember the statement and its values: the old
    // QSqlQuery is invalidated by reconnecting.
    const QString sql = query.lastQuery();
    QVariantList values;
    for (int i = 0; i < query.boundValues().size(); ++i)
        values.append(query.boundValue(i));

    if (!reconnect()) {
        m_lastError = tr("Sin conexion con el servidor de datos: %1").arg(m_lastError);
        return false;
    }
    if (inTransaction) {
        m_lastError = tr("Se perdio la conexion con el servidor durante la operacion; "
                         "ya se reconecto. Intente de nuevo.");
        return false;
    }

    query = QSqlQuery(database());
    bool ok;
    if (values.isEmpty()) {
        ok = query.exec(sql);
    } else {
        query.prepare(sql);
        for (const QVariant &v : qAsConst(values))
            query.addBindValue(v);
        ok = query.exec();
    }
    if (!ok)
        m_lastError = query.lastError().text();
    return ok;
}

void ConnectionMonitor::healthCheck()
{
    if (m_probePending)
        return;                     // previous check still waiting on the network
    m_probePending = true;
    emit probeRequested();          // runs on the probe thread
}

void ConnectionMonitor::onProbeResult(bool reachable, const QString &serverStart, const QString &error)
{
    m_probePending = false;
    if (m_interval <= 0)
        return;                     // checks were turned off meanwhile

    if (!reachable) {
        setConnected(false, error);
        return;
    }

    // The server answers. Reopen our connection if we were cut off, if it
    // broke, or if the server restarted since we connected (the connection
    // died with the old server process, though nothing has used it yet).
    const bool restarted = !m_serverStart.isEmpty() && serverStart != m_serverStart;
    if (!m_connected || restarted || connectionBroken()) {
        reopen();
    } else if (m_serverStart.isEmpty()) {
        m_serverStart = serverStart;
    }
}

/// Safety net for LISTEN/NOTIFY. Qt's driver reads notifications when a
/// socket notifier says data arrived, but on Windows (Qt 5.15) the notifier
/// created for a reopened connection can go quiet after its first event,
/// leaving the till deaf to the others. This asks the driver to read what's
/// waiting, the same thing the notifier would do: a non-blocking read, so
/// at worst a change from another till shows up a second late.
void ConnectionMonitor::pollNotifications()
{
    if (!m_connected)
        return;
    QSqlDatabase db = database();
    if (!db.isOpen() || db.driver()->subscribedToNotifications().isEmpty())
        return;
    QMetaObject::invokeMethod(db.driver(), "_q_handleNotification", Qt::DirectConnection);
}

// --- ServerProbe ------------------------------------------------------------

ServerProbe::ServerProbe(const QString &sourceConnection)
    : m_source(sourceConnection), m_name(sourceConnection + QStringLiteral("-probe"))
{
}

void ServerProbe::check()
{
    QSqlDatabase db = QSqlDatabase::database(m_name, false);
    if (!db.isValid())  // first check: same settings as the main connection, owned by this thread
        db = QSqlDatabase::cloneDatabase(m_source, m_name);

    auto ask = [&db](QString *answer, QString *error) {
        if (!db.isOpen() && !db.open()) {
            *error = db.lastError().text();
            return false;
        }
        QSqlQuery q(db);
        if (q.exec(QString::fromLatin1(kServerStartSql)) && q.next()) {
            *answer = q.value(0).toString();
            return true;
        }
        *error = q.lastError().text();
        return false;
    };

    QString answer, error;
    bool ok = ask(&answer, &error);
    if (!ok && db.isOpen()) {
        // our own connection may have died with a server restart: try a fresh one
        db.close();
        ok = ask(&answer, &error);
    }
    if (!ok)
        db.close();             // start clean next time
    emit result(ok, answer, error);
}

void ServerProbe::shutdown()
{
    {
        QSqlDatabase db = QSqlDatabase::database(m_name, false);
        if (!db.isValid())
            return;
        db.close();
    }
    QSqlDatabase::removeDatabase(m_name);
}
