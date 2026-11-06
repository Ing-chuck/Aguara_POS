// What leaves the computer, and when. A crash for real, in a child process.
// With AGUARA_TEST_TELEMETRY_URL and _KEY, also against the real server of the
// Aguara_Telemetry project: its protocol refuses anything it doesn't know.
#include "activitylog.h"
#include "crashhandler.h"
#include "database.h"
#include "logging.h"
#include "telemetry.h"
#include "telemetrydialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtEndian>
#include <QUuid>
#include <QtTest>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

const char kSender[] = "00000000-0000-4000-8000-000000000001";

/// Answers every request with one status, and keeps what it was sent.
class FakeServer : public QObject
{
public:
    explicit FakeServer(int status) : status(status)
    {
        QVERIFY(server.listen(QHostAddress::LocalHost));
        connect(&server, &QTcpServer::newConnection, this, [this]() {
            QTcpSocket *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { read(socket); });
        });
    }

    QString url() const { return QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()); }

    QByteArray head, body;
    int requests = 0;

private:
    void read(QTcpSocket *socket)
    {
        incoming += socket->readAll();
        const int split = incoming.indexOf("\r\n\r\n");
        if (split < 0)
            return;
        const QByteArray headers = incoming.left(split).toLower();
        const int at = headers.indexOf("content-length:");
        const int length = headers.mid(at + 15, headers.indexOf("\r\n", at) - at - 15).trimmed().toInt();
        if (incoming.size() < split + 4 + length)
            return;
        head = incoming.left(split);
        body = incoming.mid(split + 4, length);
        incoming.clear();
        ++requests;
        socket->write(QStringLiteral("HTTP/1.1 %1 X\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}").arg(status).toLatin1());
        socket->disconnectFromHost();
    }

    QTcpServer server;
    QByteArray incoming;
    int status;
};

TelemetrySettings on(const QString &endpoint = QStringLiteral("http://127.0.0.1:9"))
{
    TelemetrySettings s;
    s.enabled = true;
    s.endpoint = endpoint;
    s.key = QStringLiteral("clave");
    s.installName = QStringLiteral("Prueba");
    s.installId = QString::fromLatin1(kSender);
    return s;
}

QJsonObject parsed(const QByteArray &json)
{
    return QJsonDocument::fromJson(json).object();
}

int usageOf(const QJsonObject &report, const QString &name)
{
    int total = 0;
    for (const QJsonValue &row : report.value(QStringLiteral("usage")).toArray()) {
        if (row[QStringLiteral("name")].toString() == name)
            total += row[QStringLiteral("count")].toInt();
    }
    return total;
}

QStringList keysOf(const QJsonObject &object)
{
    QStringList keys = object.keys();
    keys.sort();
    return keys;
}

} // namespace

class tst_Telemetry : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void activityIsLocalAndAlwaysOn();
    void activityOnlyHasPlainNames();
    void activityGoesAtOnce();
    void activityKeepsAYear();
    void aRunThatDidntEndIsNoticed();
    void crashesStayWithTelemetryOff();
    void aSentCrashStaysHereAndDoesntGoTwice();
    void offCollectsNothing();
    void countsEventsPerDay();
    void onlyPlainNamesAreCounted();
    void countsSurviveARestart();
    void reportHoldsOnlyWhatTheProtocolSays();
    void delivered();
    void recentDaysGoAgainAndOldOnesGo();
    void serverDownKeepsEverything();
    void wrongKeyKeepsEverything();
    void aSlowServerIsNotSentTwiceAtOnce();
    void bugsWaitForTheServer();
    void crashLeavesAReport_data();
    void crashLeavesAReport();
    void settingsKeepTheKeyEncrypted();
    void settingsAreChecked_data();
    void settingsAreChecked();
    void dialogRefusesWhatItCantUse();
    void dialogTurnsItOffWithoutAServer();
    void dialogShowsExactlyWhatGoes();
    void bugFormNeedsAText();
    void againstTheRealServer();

private:
    QString folder() const { return m_tmp->path(); }

    QScopedPointer<QTemporaryDir> m_tmp;
};

void tst_Telemetry::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void tst_Telemetry::init()
{
    m_tmp.reset(new QTemporaryDir);
    QVERIFY(m_tmp->isValid());
}

static QString activityText(const QString &directory)
{
    QFile file(QDir(directory).filePath(QStringLiteral("actividad-%1.log").arg(QDate::currentDate().toString(QStringLiteral("yyyy-MM")))));
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

void tst_Telemetry::activityIsLocalAndAlwaysOn()
{
    QVERIFY(ActivityLog::open(folder()));
    Telemetry telemetry(folder());
    QVERIFY(!telemetry.isEnabled());
    Telemetry::count(QStringLiteral("venta.cobrar"));
    Telemetry::count(QStringLiteral("reporte.iva.libro"));
    ActivityLog::close();

    const QStringList lines = activityText(folder()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QCOMPARE(lines.size(), 2);
    QVERIFY(QRegularExpression(QStringLiteral("^\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2} venta\\.cobrar$")).match(lines.first().trimmed()).hasMatch());
    QVERIFY(parsed(telemetry.report())[QStringLiteral("usage")].toArray().isEmpty());     // none of it is for telemetry
}

void tst_Telemetry::activityOnlyHasPlainNames()
{
    QVERIFY(ActivityLog::open(folder()));
    for (const char *text : { "Ana Pérez", "cliente: juan", "80009735-1 " })
        Telemetry::count(QString::fromUtf8(text));
    ActivityLog::close();
    QVERIFY(activityText(folder()).isEmpty());
}

void tst_Telemetry::activityGoesAtOnce()
{
    QVERIFY(ActivityLog::open(folder()));
    Telemetry::count(QStringLiteral("venta.cobrar"));
    QVERIFY(activityText(folder()).contains(QStringLiteral("venta.cobrar")));    // not waiting for a close that a crash won't reach
    ActivityLog::close();
}

void tst_Telemetry::activityKeepsAYear()
{
    for (int month = 1; month <= 12; ++month) {
        for (int year : { 2020, 2021 }) {
            QFile old(QDir(folder()).filePath(QStringLiteral("actividad-%1-%2.log").arg(year).arg(month, 2, 10, QLatin1Char('0'))));
            QVERIFY(old.open(QIODevice::WriteOnly));
        }
    }
    QVERIFY(ActivityLog::open(folder()));
    ActivityLog::close();
    QStringList kept = QDir(folder()).entryList({ QStringLiteral("actividad-*.log") }, QDir::Files, QDir::Name);
    QCOMPARE(kept.size(), int(ActivityLog::KeptMonths));
    QVERIFY(kept.contains(QStringLiteral("actividad-2021-12.log")));
    QVERIFY(!kept.contains(QStringLiteral("actividad-2020-12.log")));
}

void tst_Telemetry::aRunThatDidntEndIsNoticed()
{
    QVERIFY(ActivityLog::open(folder()));
    QVERIFY(!ActivityLog::lastRunEndedBadly());                     // the first time
    ActivityLog::close();
    QVERIFY(ActivityLog::open(folder()));
    QVERIFY(!ActivityLog::lastRunEndedBadly());                     // a normal end
    // a power cut: open() again, with nobody having called close()
    QVERIFY(ActivityLog::open(folder()));
    QVERIFY(ActivityLog::lastRunEndedBadly());
    ActivityLog::close();
}

void tst_Telemetry::crashesStayWithTelemetryOff()
{
    QProcess child;
    child.start(QCoreApplication::applicationFilePath(), { QStringLiteral("--crash"), folder() });
    QVERIFY(child.waitForFinished(20000));
    const QStringList files = QDir(folder()).entryList({ QStringLiteral("crash-*.json") });
    QCOMPARE(files.size(), 1);
    QFile file(QDir(folder()).filePath(files.first()));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QVERIFY(parsed(file.readAll()).contains(QStringLiteral("code")));     // what someone here can read
}

void tst_Telemetry::aSentCrashStaysHereAndDoesntGoTwice()
{
    QProcess child;
    QTemporaryDir crashes;
    child.start(QCoreApplication::applicationFilePath(), { QStringLiteral("--crash"), crashes.path() });
    QVERIFY(child.waitForFinished(20000));

    FakeServer server(200);
    Telemetry telemetry(folder(), crashes.path());
    telemetry.configure(on(server.url()));
    QCOMPARE(parsed(telemetry.report())[QStringLiteral("crashes")].toArray().size(), 1);
    QSignalSpy sent(&telemetry, &Telemetry::sent);
    telemetry.send();
    QVERIFY(sent.wait());
    QCOMPARE(parsed(server.body)[QStringLiteral("crashes")].toArray().size(), 1);

    QCOMPARE(QDir(crashes.path()).entryList({ QStringLiteral("crash-*.json.enviado") }).size(), 1);
    QVERIFY(QDir(crashes.path()).entryList({ QStringLiteral("crash-*.json") }).isEmpty());
    QVERIFY(parsed(telemetry.report())[QStringLiteral("crashes")].toArray().isEmpty());
}

void tst_Telemetry::offCollectsNothing()
{
    Telemetry telemetry(folder());
    QVERIFY(!telemetry.isEnabled());
    Telemetry::count(QStringLiteral("venta.cobrar"));
    telemetry.reportBug(QStringLiteral("algo"), QString());
    QCOMPARE(usageOf(parsed(telemetry.report()), QStringLiteral("venta.cobrar")), 0);
    QVERIFY(QDir(folder()).isEmpty());

    QSignalSpy sent(&telemetry, &Telemetry::sent);
    telemetry.send();
    QTest::qWait(50);
    QCOMPARE(sent.count(), 0);
}

void tst_Telemetry::countsEventsPerDay()
{
    Telemetry telemetry(folder());
    telemetry.configure(on());
    for (int i = 0; i < 3; ++i)
        Telemetry::count(QStringLiteral("venta.cobrar"));
    Telemetry::count(QStringLiteral("reporte.iva.libro"));
    const QJsonObject report = parsed(telemetry.report());
    QCOMPARE(usageOf(report, QStringLiteral("venta.cobrar")), 3);
    QCOMPARE(usageOf(report, QStringLiteral("reporte.iva.libro")), 1);
    QCOMPARE(report[QStringLiteral("usage")].toArray().first()[QStringLiteral("day")].toString(),
             QDate::currentDate().toString(Qt::ISODate));
}

void tst_Telemetry::onlyPlainNamesAreCounted()
{
    Telemetry telemetry(folder());
    telemetry.configure(on());
    for (const char *text : { "Ana Pérez", "80009735-1 ", "cliente: juan", "a b", "", "<script>" })
        Telemetry::count(QString::fromUtf8(text));
    Telemetry::count(QString(81, QLatin1Char('a')));
    QVERIFY(parsed(telemetry.report())[QStringLiteral("usage")].toArray().isEmpty());
    Telemetry::count(QStringLiteral("Modulo.Sales"));       // the same event, whatever its capitals
    QCOMPARE(usageOf(parsed(telemetry.report()), QStringLiteral("modulo.sales")), 1);
}

void tst_Telemetry::countsSurviveARestart()
{
    {
        Telemetry telemetry(folder());
        telemetry.configure(on());
        Telemetry::count(QStringLiteral("venta.cobrar"));
    }
    Telemetry telemetry(folder());
    telemetry.configure(on());
    Telemetry::count(QStringLiteral("venta.cobrar"));
    QCOMPARE(usageOf(parsed(telemetry.report()), QStringLiteral("venta.cobrar")), 2);
}

void tst_Telemetry::reportHoldsOnlyWhatTheProtocolSays()
{
    Telemetry telemetry(folder());
    telemetry.configure(on());
    telemetry.setInstall(QStringLiteral("sqlite"), { QStringLiteral("sales"), QStringLiteral("Tiene Espacios") });
    Telemetry::count(QStringLiteral("venta.cobrar"));
    telemetry.reportBug(QStringLiteral("No imprime"), QStringLiteral("caja 1"));

    const QByteArray bytes = telemetry.report();
    const QJsonObject report = parsed(bytes);
    QCOMPARE(keysOf(report), QStringList({ "bugs", "crashes", "install", "schema", "usage" }));
    const QJsonObject install = report[QStringLiteral("install")].toObject();
    QCOMPARE(keysOf(install), QStringList({ "api", "backend", "id", "modules", "name", "os", "version" }));
    QCOMPARE(install[QStringLiteral("modules")].toArray().size(), 1);
    QCOMPARE(keysOf(report[QStringLiteral("usage")].toArray().first().toObject()), QStringList({ "count", "day", "name" }));
    QCOMPARE(keysOf(report[QStringLiteral("bugs")].toArray().first().toObject()), QStringList({ "contact", "id", "text", "version", "when" }));

    QVERIFY(!QString::fromUtf8(bytes).contains(QSysInfo::machineHostName(), Qt::CaseInsensitive));
    QVERIFY(!bytes.contains("clave"));
}

void tst_Telemetry::delivered()
{
    FakeServer server(200);
    Telemetry telemetry(folder());
    telemetry.configure(on(server.url() + QStringLiteral("/")));       // the slash is common, and harmless
    telemetry.reportBug(QStringLiteral("No imprime"), QString());
    QCOMPARE(QDir(folder()).entryList({ QStringLiteral("bug-*") }).size(), 1);

    QSignalSpy sent(&telemetry, &Telemetry::sent);
    telemetry.send();
    QVERIFY(sent.wait());
    QCOMPARE(sent.first().first().toBool(), true);
    QVERIFY2(server.head.startsWith("POST /api/v1/report "), server.head.constData());
    QVERIFY(server.head.contains("Bearer clave"));
    QCOMPARE(parsed(server.body)[QStringLiteral("bugs")].toArray().size(), 1);
    QVERIFY(QDir(folder()).entryList({ QStringLiteral("bug-*") }).isEmpty());   // it won't go twice
}

void tst_Telemetry::recentDaysGoAgainAndOldOnesGo()
{
    QJsonObject stored;
    stored[QDate::currentDate().addDays(-3).toString(Qt::ISODate)] = QJsonObject{ { "reciente", 2 } };
    stored[QDate::currentDate().addDays(-30).toString(Qt::ISODate)] = QJsonObject{ { "antiguo", 5 } };
    stored[QDate::currentDate().addDays(-90).toString(Qt::ISODate)] = QJsonObject{ { "vencido", 9 } };
    QFile file(QDir(folder()).filePath(QStringLiteral("usage.json")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(stored).toJson());
    file.close();

    FakeServer server(200);
    Telemetry telemetry(folder());
    telemetry.configure(on(server.url()));
    QCOMPARE(usageOf(parsed(telemetry.report()), QStringLiteral("vencido")), 0);    // too old, even unsent

    QSignalSpy sent(&telemetry, &Telemetry::sent);
    telemetry.send();
    QVERIFY(sent.wait());
    const QJsonObject went = parsed(server.body);
    QCOMPARE(usageOf(went, QStringLiteral("antiguo")), 5);
    QCOMPARE(usageOf(went, QStringLiteral("reciente")), 2);

    const QJsonObject after = parsed(telemetry.report());
    QCOMPARE(usageOf(after, QStringLiteral("antiguo")), 0);                         // delivered, and past its days
    QCOMPARE(usageOf(after, QStringLiteral("reciente")), 2);                        // its total can still grow
}

void tst_Telemetry::serverDownKeepsEverything()
{
    Telemetry telemetry(folder());
    telemetry.configure(on(QStringLiteral("http://127.0.0.1:1")));
    Telemetry::count(QStringLiteral("venta.cobrar"));
    telemetry.reportBug(QStringLiteral("No imprime"), QString());

    QSignalSpy sent(&telemetry, &Telemetry::sent);
    telemetry.send();
    QVERIFY(sent.wait(20000));
    QCOMPARE(sent.first().first().toBool(), false);
    QCOMPARE(QDir(folder()).entryList({ QStringLiteral("bug-*") }).size(), 1);
    QCOMPARE(usageOf(parsed(telemetry.report()), QStringLiteral("venta.cobrar")), 1);
}

void tst_Telemetry::wrongKeyKeepsEverything()
{
    FakeServer server(401);
    Telemetry telemetry(folder());
    telemetry.configure(on(server.url()));
    telemetry.reportBug(QStringLiteral("No imprime"), QString());
    QSignalSpy sent(&telemetry, &Telemetry::sent);
    telemetry.send();
    QVERIFY(sent.wait());
    QCOMPARE(sent.first().first().toBool(), false);
    QCOMPARE(QDir(folder()).entryList({ QStringLiteral("bug-*") }).size(), 1);
}

void tst_Telemetry::aSlowServerIsNotSentTwiceAtOnce()
{
    FakeServer server(200);
    Telemetry telemetry(folder());
    telemetry.configure(on(server.url()));
    QSignalSpy sent(&telemetry, &Telemetry::sent);
    telemetry.send();
    telemetry.send();
    telemetry.send();
    QVERIFY(sent.wait());
    QTest::qWait(100);
    QCOMPARE(server.requests, 1);
}

void tst_Telemetry::bugsWaitForTheServer()
{
    {
        Telemetry telemetry(folder());
        telemetry.configure(on());
        telemetry.reportBug(QStringLiteral("  No imprime  "), QStringLiteral(" caja 1 "));
    }
    Telemetry telemetry(folder());
    telemetry.configure(on());
    const QJsonObject bug = parsed(telemetry.report())[QStringLiteral("bugs")].toArray().first().toObject();
    QCOMPARE(bug[QStringLiteral("text")].toString(), QStringLiteral("No imprime"));
    QCOMPARE(bug[QStringLiteral("contact")].toString(), QStringLiteral("caja 1"));
    QVERIFY(!QUuid::fromString(bug[QStringLiteral("id")].toString()).isNull());
}

void tst_Telemetry::crashLeavesAReport_data()
{
    QTest::addColumn<QString>("how");
    QTest::addColumn<QString>("code");
    QTest::newRow("a bad pointer") << "--crash" << "0xc0000005";
    QTest::newRow("a qFatal") << "--fatal" << "qfatal";
    QTest::newRow("an uncaught exception") << "--throw" << "abort";
}

void tst_Telemetry::crashLeavesAReport()
{
    QFETCH(QString, how);
    QFETCH(QString, code);

    QProcess child;
    child.start(QCoreApplication::applicationFilePath(), { how, folder() });
    QVERIFY(child.waitForFinished(20000));
    QVERIFY(child.exitStatus() == QProcess::CrashExit || child.exitCode() != 0);
    QCOMPARE(QDir(folder()).entryList({ QStringLiteral("crash-*.json") }).size(), 1);

    Telemetry telemetry(folder());
    telemetry.configure(on());
    QVERIFY(!telemetry.report().contains("Ana"));       // the message stays out
    const QJsonArray crashes = parsed(telemetry.report())[QStringLiteral("crashes")].toArray();
    QCOMPARE(crashes.size(), 1);
    const QJsonObject crash = crashes.first().toObject();
    QCOMPARE(keysOf(crash), QStringList({ "code", "frames", "id", "modules", "version", "when" }));
    QCOMPARE(crash[QStringLiteral("code")].toString(), code);
    QCOMPARE(crash[QStringLiteral("version")].toString(), QStringLiteral("1.2.3-prueba"));

    const QJsonArray frames = crash[QStringLiteral("frames")].toArray();
    QVERIFY(!frames.isEmpty());
    const QRegularExpression place(QStringLiteral("^[\\w.+-]{1,120}\\+0x[0-9a-fA-F]+$|^0x[0-9a-fA-F]+$"));
    bool own = false;
    for (const QJsonValue &frame : frames) {
        QVERIFY2(place.match(frame.toString()).hasMatch(), qPrintable(frame.toString()));      // a place, never a path
        own = own || frame.toString().startsWith(QStringLiteral("tst_telemetry.exe+"));
    }
    QVERIFY2(own, qPrintable(QString::fromUtf8(QJsonDocument(frames).toJson())));

    // each module with the stamp of this very file: that's how its symbols are found
    const QJsonObject modules = crash[QStringLiteral("modules")].toObject();
    for (const QJsonValue &frame : frames) {
        const int plus = frame.toString().indexOf(QStringLiteral("+0x"));
        if (plus > 0)
            QVERIFY2(modules.contains(frame.toString().left(plus)), qPrintable(frame.toString()));
    }
    QVERIFY(QRegularExpression(QStringLiteral("^[0-9a-f]{8}$")).match(modules[QStringLiteral("tst_telemetry.exe")].toString()).hasMatch());
    QFile self(QCoreApplication::applicationFilePath());
    QVERIFY(self.open(QIODevice::ReadOnly));
    const QByteArray image = self.read(4096);
    const quint32 at = qFromLittleEndian<quint32>(image.constData() + 0x3C);
    const quint32 stamp = qFromLittleEndian<quint32>(image.constData() + at + 8);
    QCOMPARE(modules[QStringLiteral("tst_telemetry.exe")].toString(), QStringLiteral("%1").arg(stamp, 8, 16, QLatin1Char('0')));
}

void tst_Telemetry::settingsKeepTheKeyEncrypted()
{
    QFile::remove(DatabaseSettings::settingsPath());
    TelemetrySettings s = on();
    s.key = QStringLiteral("clave-muy-secreta");
    s.save();
    const TelemetrySettings loaded = TelemetrySettings::load();
    QCOMPARE(loaded.key, s.key);
    QCOMPARE(loaded.endpoint, s.endpoint);
    QCOMPARE(loaded.installId, s.installId);
    QVERIFY(loaded.enabled);
    QFile ini(DatabaseSettings::settingsPath());
    QVERIFY(ini.open(QIODevice::ReadOnly));
    QVERIFY(!ini.readAll().contains("clave-muy-secreta"));
    ini.close();
    QFile::remove(DatabaseSettings::settingsPath());
    QVERIFY(!TelemetrySettings::load().enabled);        // off, until someone turns it on
}

void tst_Telemetry::settingsAreChecked_data()
{
    QTest::addColumn<QString>("endpoint");
    QTest::addColumn<QString>("key");
    QTest::addColumn<bool>("usable");
    QTest::newRow("https") << "https://telemetria.ejemplo.com" << "k" << true;
    QTest::newRow("http and port") << "http://192.168.1.50:8787" << "k" << true;
    QTest::newRow("with a slash") << "http://servidor:8787/" << "k" << true;
    QTest::newRow("empty") << "" << "k" << false;
    QTest::newRow("no scheme") << "servidor:8787" << "k" << false;
    QTest::newRow("another scheme") << "ftp://servidor" << "k" << false;
    QTest::newRow("spaces") << "http://mal servidor" << "k" << false;
    QTest::newRow("no key") << "http://servidor" << "  " << false;
}

void tst_Telemetry::settingsAreChecked()
{
    QFETCH(QString, endpoint);
    QFETCH(QString, key);
    QFETCH(bool, usable);
    TelemetrySettings s;
    s.endpoint = endpoint;
    s.key = key;
    QCOMPARE(s.problem().isEmpty(), usable);
}

void tst_Telemetry::dialogRefusesWhatItCantUse()
{
    QFile::remove(DatabaseSettings::settingsPath());
    Telemetry telemetry(folder());
    TelemetryDialog dialog(&telemetry);
    dialog.show();
    dialog.findChild<QCheckBox *>(QStringLiteral("telemetryEnabled"))->setChecked(true);
    dialog.findChild<QLineEdit *>(QStringLiteral("telemetryEndpoint"))->setText(QStringLiteral("no es una dirección"));
    dialog.accept();
    QVERIFY(dialog.isVisible());
    QVERIFY(!telemetry.isEnabled());
    QVERIFY(!dialog.findChild<QLabel *>(QStringLiteral("telemetryStatus"))->isHidden());

    dialog.findChild<QLineEdit *>(QStringLiteral("telemetryEndpoint"))->setText(QStringLiteral("http://127.0.0.1:9"));
    dialog.findChild<QLineEdit *>(QStringLiteral("telemetryKey"))->setText(QStringLiteral("clave"));
    dialog.accept();
    QVERIFY(!dialog.isVisible());
    QVERIFY(telemetry.isEnabled());
    QVERIFY(TelemetrySettings::load().enabled);
    QVERIFY(!TelemetrySettings::load().installId.isEmpty());            // created when turned on
    telemetry.configure(TelemetrySettings());
    QFile::remove(DatabaseSettings::settingsPath());
}

void tst_Telemetry::dialogTurnsItOffWithoutAServer()
{
    Telemetry telemetry(folder());
    telemetry.configure(on());
    TelemetryDialog dialog(&telemetry);
    dialog.show();
    dialog.findChild<QCheckBox *>(QStringLiteral("telemetryEnabled"))->setChecked(false);
    QVERIFY(!dialog.findChild<QLineEdit *>(QStringLiteral("telemetryEndpoint"))->isEnabled());
    dialog.findChild<QLineEdit *>(QStringLiteral("telemetryEndpoint"))->clear();
    dialog.accept();
    QVERIFY(!dialog.isVisible());                                       // nothing to check to stop sending
    QVERIFY(!telemetry.isEnabled());
    Telemetry::count(QStringLiteral("venta.cobrar"));
    QCOMPARE(usageOf(parsed(telemetry.report()), QStringLiteral("venta.cobrar")), 0);
    QFile::remove(DatabaseSettings::settingsPath());
}

void tst_Telemetry::dialogShowsExactlyWhatGoes()
{
    Telemetry telemetry(folder());
    telemetry.configure(on());
    Telemetry::count(QStringLiteral("venta.cobrar"));
    TelemetryDialog dialog(&telemetry);
    bool shown = false;
    QTimer::singleShot(150, this, [&]() {
        auto view = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        QVERIFY(view);
        const QString text = view->findChild<QPlainTextEdit *>(QStringLiteral("telemetryReport"))->toPlainText();
        QCOMPARE(text.toUtf8(), telemetry.report());
        shown = text.contains(QStringLiteral("venta.cobrar"));
        view->reject();
    });
    dialog.findChild<QPushButton *>(QStringLiteral("telemetryShow"))->click();
    QVERIFY(shown);
}

void tst_Telemetry::bugFormNeedsAText()
{
    Telemetry telemetry(folder());
    telemetry.configure(on());
    BugReportDialog dialog(&telemetry);
    dialog.show();
    dialog.accept();
    QVERIFY(dialog.isVisible());
    QVERIFY(!dialog.findChild<QLabel *>(QStringLiteral("bugError"))->isHidden());
    dialog.findChild<QPlainTextEdit *>(QStringLiteral("bugText"))->setPlainText(QString(4001, QLatin1Char('x')));
    dialog.accept();
    QVERIFY(dialog.isVisible());
    dialog.findChild<QPlainTextEdit *>(QStringLiteral("bugText"))->setPlainText(QStringLiteral("No imprime"));
    dialog.findChild<QLineEdit *>(QStringLiteral("bugContact"))->setText(QStringLiteral("caja 1"));
    dialog.accept();
    QVERIFY(!dialog.isVisible());
    QCOMPARE(QDir(folder()).entryList({ QStringLiteral("bug-*") }).size(), 1);
}

void tst_Telemetry::againstTheRealServer()
{
    const QString url = qEnvironmentVariable("AGUARA_TEST_TELEMETRY_URL");
    if (url.isEmpty())
        QSKIP("No real server: set AGUARA_TEST_TELEMETRY_URL and AGUARA_TEST_TELEMETRY_KEY");

    QProcess child;
    child.start(QCoreApplication::applicationFilePath(), { QStringLiteral("--crash"), folder() });
    QVERIFY(child.waitForFinished(20000));

    TelemetrySettings settings = on(url);
    settings.key = qEnvironmentVariable("AGUARA_TEST_TELEMETRY_KEY");
    settings.installId = QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
    settings.installName = QStringLiteral("Cliente ñandú real");
    Telemetry telemetry(folder());
    telemetry.configure(settings);
    telemetry.setInstall(QStringLiteral("sqlite"), { QStringLiteral("sales"), QStringLiteral("inventory") });
    Telemetry::count(QStringLiteral("venta.cobrar"));
    Telemetry::count(QStringLiteral("reporte.iva.libro"));
    telemetry.reportBug(QStringLiteral("Prueba con acentos: ñandú \"comillas\" <b>\nsegunda línea"), QStringLiteral("caja 1"));

    bool answered = false, pinged = false;
    QString text;
    telemetry.ping(settings, [&](bool ok, const QString &message) {
        answered = true;
        pinged = ok;
        text = message;
    });
    QTRY_VERIFY(answered);
    QVERIFY2(pinged, qPrintable(text));

    QSignalSpy sent(&telemetry, &Telemetry::sent);
    telemetry.send();
    QVERIFY(sent.wait(15000));
    QVERIFY2(sent.first().first().toBool(), "the server refused the report");
    QVERIFY(QDir(folder()).entryList({ QStringLiteral("crash-*.json"), QStringLiteral("bug-*") }).isEmpty());
}

static void crashForReal(const QString &how, const QString &directory)
{
#ifdef Q_OS_WIN
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);   // no dialog waiting for someone
#endif
    QDir().mkpath(directory);
    CrashHandler::install(directory, QStringLiteral("1.2.3-prueba"));
    if (how == QLatin1String("--fatal")) {
        AguaraLog::install(QDir(directory).filePath(QStringLiteral("logs")), false);
        qFatal("on purpose: datos del cliente Ana");
    }
    if (how == QLatin1String("--throw"))
        throw 1;
    volatile int *nowhere = nullptr;
    *nowhere = 1;
}

int main(int argc, char **argv)
{
    if (argc == 3 && (qstrcmp(argv[1], "--crash") == 0 || qstrcmp(argv[1], "--fatal") == 0 || qstrcmp(argv[1], "--throw") == 0)) {
        QCoreApplication app(argc, argv);
        crashForReal(QString::fromLocal8Bit(argv[1]), QString::fromLocal8Bit(argv[2]));
        return 0;
    }
    QApplication app(argc, argv);
    tst_Telemetry test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_telemetry.moc"
