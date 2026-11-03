// Opening the database and the migrations, on SQLite and PostgreSQL.
#include "testsupport.h"

#include <QSqlQuery>
#include <QtTest>

using TestSupport::CoreFixture;

class tst_Database : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase_data() { TestSupport::addBackendRows(); }
    void cleanup() { m_core.close(); }

    void migratesToCurrentVersion();
    void migrationIsIdempotent();
    void otherOwnersHaveTheirOwnVersion();
    void failedMigrationRollsBack();
    void sqlitePragmas();
    void noDefaultConnection();
    void reopenAfterClose();
    void openErrorsAreForTheUser();

private:
    QTemporaryDir m_tmp;
    CoreFixture m_core;
};

void tst_Database::migratesToCurrentVersion()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QCOMPARE(m_core.database.schemaVersion(QStringLiteral("core")), coreMigrations().last().version);

    QSqlQuery q(m_core.connection());
    QVERIFY(q.exec(QStringLiteral("SELECT tasa FROM iva_cat ORDER BY tasa DESC")));
    QList<int> rates;
    while (q.next())
        rates << q.value(0).toInt();
    QCOMPARE(rates, QList<int>({ 10, 5, 0 }));
}

void tst_Database::migrationIsIdempotent()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(m_core.database.migrate(QStringLiteral("core"), coreMigrations()));
    QVERIFY(m_core.database.migrate(QStringLiteral("core"), coreMigrations()));
    QSqlQuery q(m_core.connection());
    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM schema_migrations WHERE owner = 'core'")) && q.next());
    QCOMPARE(q.value(0).toInt(), coreMigrations().size());
}

void tst_Database::otherOwnersHaveTheirOwnVersion()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    Migration m;
    m.version = 1;
    m.description = QStringLiteral("module table");
    m.sqlite = m.postgresql = QStringList{ QStringLiteral("CREATE TABLE test_module_t (id INTEGER PRIMARY KEY)") };
    QCOMPARE(m_core.database.schemaVersion(QStringLiteral("test_module")), 0);
    QVERIFY(m_core.database.migrate(QStringLiteral("test_module"), { m }));
    QCOMPARE(m_core.database.schemaVersion(QStringLiteral("test_module")), 1);
    QCOMPARE(m_core.database.schemaVersion(QStringLiteral("core")), coreMigrations().last().version);
}

void tst_Database::failedMigrationRollsBack()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    Migration bad;
    bad.version = 1;
    bad.description = QStringLiteral("second statement fails");
    bad.sqlite = bad.postgresql = QStringList{
        QStringLiteral("CREATE TABLE half_done (id INTEGER)"),
        QStringLiteral("THIS IS NOT SQL") };
    QVERIFY(!m_core.database.migrate(QStringLiteral("broken"), { bad }));
    QCOMPARE(m_core.database.schemaVersion(QStringLiteral("broken")), 0);

    // the first statement was rolled back with the rest
    QSqlQuery q(m_core.connection());
    QVERIFY(!q.exec(QStringLiteral("SELECT * FROM half_done")));

    // the user reads Spanish, not the SQL error
    const QString message = m_core.database.lastError();
    QVERIFY2(message.contains(QString::fromUtf8("No se pudo actualizar")), qPrintable(message));
    QVERIFY2(!message.contains(QStringLiteral("syntax")), qPrintable(message));
}

void tst_Database::sqlitePragmas()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    if (m_core.database.backend() != DatabaseSettings::SQLite)
        QSKIP("SQLite only");
    QSqlQuery q(m_core.connection());
    QVERIFY(q.exec(QStringLiteral("PRAGMA foreign_keys")) && q.next());
    QCOMPARE(q.value(0).toInt(), 1);
    QVERIFY(q.exec(QStringLiteral("PRAGMA journal_mode")) && q.next());
    QCOMPARE(q.value(0).toString(), QStringLiteral("wal"));
    QVERIFY(q.exec(QStringLiteral("PRAGMA synchronous")) && q.next());
    QCOMPARE(q.value(0).toInt(), 2);    // FULL
}

void tst_Database::noDefaultConnection()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    // only code handed the connection name reaches the data
    QVERIFY(!QSqlDatabase::database(QLatin1String(QSqlDatabase::defaultConnection), false).isValid());
}

void tst_Database::reopenAfterClose()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    const DatabaseSettings settings = m_core.settings;
    m_core.close();
    Database again(QStringLiteral("again"));
    QVERIFY2(again.open(settings), qPrintable(again.lastError()));
    QVERIFY(again.migrate(QStringLiteral("core"), coreMigrations()));
    QCOMPARE(again.schemaVersion(QStringLiteral("core")), coreMigrations().last().version);
}

void tst_Database::openErrorsAreForTheUser()
{
    QFETCH_GLOBAL(int, backend);
    DatabaseSettings s;
    if (backend == DatabaseSettings::PostgreSQL) {
        // nothing listens on port 1: fails fast with "connection refused"
        s.backend = DatabaseSettings::PostgreSQL;
        s.host = QStringLiteral("127.0.0.1");
        s.port = 1;
    } else {
        s.sqlitePath = m_tmp.filePath(QStringLiteral("not-a-db.db"));
        QFile f(s.sqlitePath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(4096, 'x'));             // garbage, not SQLite
    }
    Database db(QStringLiteral("broken"));
    QVERIFY(!db.open(s) || !db.migrate(QStringLiteral("core"), coreMigrations()));
    const QString message = db.lastError();
    QVERIFY(!message.isEmpty());
    QVERIFY2(!message.contains(QStringLiteral("QPSQL")) && !message.contains(QStringLiteral("QSQLITE")),
             qPrintable(message));
    if (backend == DatabaseSettings::PostgreSQL) {
        // without libpq.dll next to the program the driver can't load: that has its own message
        bool clientInstalled = false;
        {
            QSqlDatabase probe = QSqlDatabase::addDatabase(QStringLiteral("QPSQL"), QStringLiteral("probe"));
            clientInstalled = probe.isValid();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("probe"));
        const QString expected = clientInstalled ? QStringLiteral("No se puede conectar")
                                                 : QStringLiteral("Falta en este equipo el componente");
        QVERIFY2(message.contains(expected), qPrintable(message));
    } else
        QVERIFY2(message.contains(QString::fromUtf8("dañado")), qPrintable(message));
}

QTEST_GUILESS_MAIN(tst_Database)
#include "tst_database.moc"
