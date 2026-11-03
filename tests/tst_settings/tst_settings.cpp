// aguara.ini: settings round-trip, and the database password never stored
// in plain text (Windows DPAPI).
#include "database.h"

#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <QtTest>

class tst_Settings : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init() { QFile::remove(DatabaseSettings::settingsPath()); }
    void cleanupTestCase() { QFile::remove(DatabaseSettings::settingsPath()); }

    void roundTrip();
    void passwordIsNotStoredInPlainText();
    void oldPlainPasswordIsEncryptedOnLoad();
    void emptyPasswordRemovesIt();
    void defaults();

private:
    static QByteArray iniBytes()
    {
        QFile f(DatabaseSettings::settingsPath());
        f.open(QIODevice::ReadOnly);
        return f.readAll();
    }
};

void tst_Settings::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);   // never touch the real %APPDATA%\Aguara
    QCoreApplication::setOrganizationName(QStringLiteral("AguaraTest"));
    QCoreApplication::setApplicationName(QStringLiteral("tst_settings"));
    QVERIFY(!DatabaseSettings::settingsPath().isEmpty());
}

void tst_Settings::roundTrip()
{
    DatabaseSettings s;
    s.backend  = DatabaseSettings::PostgreSQL;
    s.host     = QStringLiteral("192.168.1.10");
    s.port     = 5433;
    s.name     = QStringLiteral("tienda");
    s.user     = QStringLiteral("caja");
    s.password = QString::fromUtf8("contraseña segura");
    s.save();

    const DatabaseSettings loaded = DatabaseSettings::load();
    QCOMPARE(int(loaded.backend), int(DatabaseSettings::PostgreSQL));
    QCOMPARE(loaded.host, s.host);
    QCOMPARE(loaded.port, 5433);
    QCOMPARE(loaded.name, s.name);
    QCOMPARE(loaded.user, s.user);
    QCOMPARE(loaded.password, s.password);
}

void tst_Settings::passwordIsNotStoredInPlainText()
{
    DatabaseSettings s;
    s.password = QStringLiteral("secreto123");
    s.save();

    const QByteArray ini = iniBytes();
    QVERIFY(!ini.isEmpty());
    QVERIFY2(!ini.contains("secreto123"), ini.constData());
    QVERIFY(ini.contains("password_protected="));
    QVERIFY(!ini.contains("\npassword="));
}

void tst_Settings::oldPlainPasswordIsEncryptedOnLoad()
{
    {   // what an older version wrote
        QSettings ini(DatabaseSettings::settingsPath(), QSettings::IniFormat);
        ini.setValue(QStringLiteral("database/backend"), QStringLiteral("postgresql"));
        ini.setValue(QStringLiteral("database/password"), QStringLiteral("viejo"));
    }
    QCOMPARE(DatabaseSettings::load().password, QStringLiteral("viejo"));
    QVERIFY(!iniBytes().contains("viejo"));                         // rewritten encrypted
    QCOMPARE(DatabaseSettings::load().password, QStringLiteral("viejo"));
}

void tst_Settings::emptyPasswordRemovesIt()
{
    DatabaseSettings s;
    s.password = QStringLiteral("x");
    s.save();
    s.password.clear();                     // use pgpass.conf instead
    s.save();
    QVERIFY(!iniBytes().contains("password"));
    QVERIFY(DatabaseSettings::load().password.isEmpty());
}

void tst_Settings::defaults()
{
    const DatabaseSettings s = DatabaseSettings::load();    // no file
    QCOMPARE(int(s.backend), int(DatabaseSettings::SQLite));
    QCOMPARE(s.port, 5432);
    QCOMPARE(s.name, QStringLiteral("aguara"));
    QVERIFY(s.describe().startsWith(QStringLiteral("SQLite: ")));
}

QTEST_GUILESS_MAIN(tst_Settings)
#include "tst_settings.moc"
