// Copies of the local (SQLite) database: a hot copy that opens and has the
// data, and the daily automatic copy with its retention.
#include "backup.h"
#include "testsupport.h"

#include <QDir>
#include <QFile>
#include <QSqlQuery>
#include <QtTest>

using TestSupport::CoreFixture;

class tst_Backup : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup() { m_core.close(); }

    void copyHasTheData();
    void copyReplacesExistingFile();
    void automaticOncePerDay();
    void automaticKeepsNewest();
    void notForPostgres();

private:
    static int productsIn(const QString &file);

    QTemporaryDir m_tmp;
    CoreFixture m_core;
};

void tst_Backup::init()
{
    QString error;
    QVERIFY2(m_core.open(DatabaseSettings::SQLite, m_tmp.path(), &error), qPrintable(error));
    Product p; p.code = QStringLiteral("1"); p.description = QStringLiteral("PAN"); p.price = 3000;
    QVERIFY(m_core.products->saveProduct(p));
}

int tst_Backup::productsIn(const QString &file)
{
    int count = -1;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("check"));
        db.setDatabaseName(file);
        if (db.open()) {
            QSqlQuery q(db);
            if (q.exec(QStringLiteral("SELECT COUNT(*) FROM productos")) && q.next())
                count = q.value(0).toInt();
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("check"));
    return count;
}

void tst_Backup::copyHasTheData()
{
    Backup backup(&m_core.database);
    QVERIFY(backup.isAvailable());
    const QString file = m_tmp.filePath(QStringLiteral("sub dir/copia.db"));    // folder created, spaces ok
    QVERIFY2(backup.backupTo(file), qPrintable(backup.lastError()));
    QCOMPARE(productsIn(file), 1);

    // the program keeps working on the original
    Product p; p.code = QStringLiteral("2"); p.description = QStringLiteral("LECHE"); p.price = 6000;
    QVERIFY(m_core.products->saveProduct(p));
    QCOMPARE(productsIn(file), 1);
}

void tst_Backup::copyReplacesExistingFile()
{
    Backup backup(&m_core.database);
    const QString file = m_tmp.filePath(QStringLiteral("copia.db"));
    { QFile f(file); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("old"); }
    QVERIFY2(backup.backupTo(file), qPrintable(backup.lastError()));
    QCOMPARE(productsIn(file), 1);
}

void tst_Backup::automaticOncePerDay()
{
    Backup backup(&m_core.database);
    const QString dir = m_tmp.filePath(QStringLiteral("auto1"));
    QString made;
    QVERIFY(backup.automatic(dir, 14, 24 * 3600, &made));
    QVERIFY(!made.isEmpty() && QFile::exists(made));
    QVERIFY(QFileInfo(made).fileName().startsWith(QStringLiteral("aguara-")));

    QVERIFY(backup.automatic(dir, 14, 24 * 3600, &made));      // same day: nothing new
    QVERIFY(made.isEmpty());
    QCOMPARE(QDir(dir).entryList(QDir::Files).size(), 1);
}

void tst_Backup::automaticKeepsNewest()
{
    Backup backup(&m_core.database);
    const QString dir = m_tmp.filePath(QStringLiteral("auto2"));
    QDir().mkpath(dir);
    // five older copies from previous days
    const QDateTime now = QDateTime::currentDateTime();
    QStringList old;
    for (int day = 5; day >= 1; --day) {
        const QString name = Backup::fileNameFor(now.addDays(-day));
        QFile f(QDir(dir).filePath(name)); QVERIFY(f.open(QIODevice::WriteOnly));
        old << name;
    }
    QFile other(QDir(dir).filePath(QStringLiteral("notes.txt"))); QVERIFY(other.open(QIODevice::WriteOnly)); other.close();

    QString made;
    QVERIFY(backup.automatic(dir, 3, 24 * 3600, &made));
    QVERIFY(!made.isEmpty());
    const QStringList left = QDir(dir).entryList({ QStringLiteral("aguara-*.db") }, QDir::Files, QDir::Name);
    QCOMPARE(left, QStringList({ old.at(3), old.at(4), QFileInfo(made).fileName() }));   // newest 3
    QVERIFY(QFile::exists(QDir(dir).filePath(QStringLiteral("notes.txt"))));            // other files untouched
}

void tst_Backup::notForPostgres()
{
    Backup none(nullptr);
    QVERIFY(!none.isAvailable());
    QVERIFY(!none.backupTo(m_tmp.filePath(QStringLiteral("x.db"))));
    QVERIFY(!none.lastError().isEmpty());
    QString made;
    QVERIFY(none.automatic(m_tmp.filePath(QStringLiteral("auto3")), 14, 0, &made));   // nothing to do, not an error
    QVERIFY(made.isEmpty());
}

QTEST_GUILESS_MAIN(tst_Backup)
#include "tst_backup.moc"
