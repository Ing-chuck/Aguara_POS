// The log file: messages land in it with level and category, debug only on
// request, and it rotates instead of growing forever.
#include "logging.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class tst_Logging : public QObject
{
    Q_OBJECT

private slots:
    void cleanup() { AguaraLog::uninstall(); }

    void writesLevelAndCategory();
    void debugOnlyWhenEnabled();
    void rotates();

private:
    static QString read(const QString &file)
    {
        QFile f(file);
        f.open(QIODevice::ReadOnly | QIODevice::Text);
        return QString::fromUtf8(f.readAll());
    }
};

void tst_Logging::writesLevelAndCategory()
{
    QTemporaryDir dir;
    QVERIFY(AguaraLog::install(dir.path(), false));
    QCOMPARE(AguaraLog::filePath(), QDir(dir.path()).filePath("aguara.log"));

    qCWarning(lcDatabase) << "connection lost";
    qCInfo(lcApp) << "started";
    qWarning("acentos: Código");

    const QString log = read(AguaraLog::filePath());
    QVERIFY2(log.contains("WARN  [aguara.db] connection lost"), qPrintable(log));
    QVERIFY(log.contains("INFO  [aguara.app] started"));
    QVERIFY(log.contains(QString::fromUtf8("[default] acentos: Código")));    // UTF-8
}

void tst_Logging::debugOnlyWhenEnabled()
{
    QTemporaryDir dir;
    QVERIFY(AguaraLog::install(dir.path(), false));
    qCDebug(lcApp) << "hidden detail";
    AguaraLog::uninstall();
    QVERIFY(!read(QDir(dir.path()).filePath("aguara.log")).contains("hidden detail"));

    QVERIFY(AguaraLog::install(dir.path(), true));
    qCDebug(lcApp) << "shown detail";
    QVERIFY(read(AguaraLog::filePath()).contains("DEBUG [aguara.app] shown detail"));
}

void tst_Logging::rotates()
{
    QTemporaryDir dir;
    QVERIFY(AguaraLog::install(dir.path(), false, 1024, 3));   // 1 KB files, keep 3
    const QString line(200, QLatin1Char('x'));
    for (int i = 0; i < 40; ++i)                               // ~8 KB in total
        qWarning("%d %s", i, qPrintable(line));
    AguaraLog::uninstall();

    const QDir d(dir.path());
    QVERIFY(d.exists("aguara.log"));
    QVERIFY(d.exists("aguara.1.log"));
    QVERIFY(d.exists("aguara.2.log"));
    QVERIFY(!d.exists("aguara.3.log"));                        // the oldest were deleted
    QVERIFY(QFileInfo(d.filePath("aguara.1.log")).size() < 2048);
    QVERIFY(read(d.filePath("aguara.log")).contains("39 x"));  // newest in the current file
}

QTEST_GUILESS_MAIN(tst_Logging)
#include "tst_logging.moc"
