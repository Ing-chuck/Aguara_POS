#include "connectionmonitor.h"
#include "corehost.h"
#include "database.h"
#include "databasesettingsdialog.h"
#include "mainwindow.h"
#include "sqlproductservice.h"
#include <version.h>

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    // Determines the data folder: %APPDATA%\Aguara\AguaraPOS
    QCoreApplication::setOrganizationName(QStringLiteral("Aguara"));
    QCoreApplication::setApplicationName(QStringLiteral("AguaraPOS"));
    QCoreApplication::setApplicationVersion(QStringLiteral(VERSION_STRING));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("AguaraPOS"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption databaseOption(QStringLiteral("database"),
        QCoreApplication::translate("main", "Use this SQLite database file, ignoring aguara.ini."),
        QStringLiteral("file"));
    QCommandLineOption configureOption(QStringLiteral("configure-database"),
        QCoreApplication::translate("main", "Show the database settings before starting (saved in %1).")
            .arg(QDir::toNativeSeparators(DatabaseSettings::settingsPath())));
    parser.addOption(databaseOption);
    parser.addOption(configureOption);
    parser.process(a);

    DatabaseSettings settings = DatabaseSettings::load();
    const bool explicitFile = parser.isSet(databaseOption);
    if (explicitFile) {
        settings.backend = DatabaseSettings::SQLite;
        settings.sqlitePath = parser.value(databaseOption);
    }

    // The core owns the data: open the database and bring its schema up to
    // date before any module is loaded. If that fails, let the user fix the
    // settings (e.g. server address) and retry, or quit.
    Database database;
    bool showSettings = parser.isSet(configureOption);
    for (;;) {
        if (!showSettings && database.open(settings)
            && database.migrate(QStringLiteral("core"), coreMigrations()))
            break;

        DatabaseSettingsDialog dialog(settings, showSettings ? QString() : database.lastError());
        database.close();
        if (dialog.exec() != QDialog::Accepted)
            return 1;
        settings = dialog.settings();
        if (!explicitFile)
            settings.save();
        showSettings = false;
    }

    // Keeps a PostgreSQL connection alive (reconnects after a server restart
    // or network failure); a no-op for SQLite.
    ConnectionMonitor connection(database.connectionName(),
                                 database.backend() == DatabaseSettings::PostgreSQL);

    // Services modules can use, looked up through the host.
    SqlProductService products(&connection);
    CoreHost host;
    host.registerService(ProductService_iid, &products);

    // Tell the user when the server is unreachable and when it's back
    // (same sender+id, so the second message replaces the first).
    QObject::connect(&connection, &ConnectionMonitor::connectionLost, &host, [&host](const QString &error) {
        host.notify(ModuleMsg(QStringLiteral("Aguara"), QStringLiteral("db-connection"),
            QCoreApplication::translate("main", "Sin conexion con el servidor"),
            QCoreApplication::translate("main", "No se puede acceder a los datos. Se reintenta automaticamente.\n%1").arg(error),
            MsgPriority::PriorityVeryHigh));
    });
    QObject::connect(&connection, &ConnectionMonitor::reconnected, &host, [&host]() {
        host.notify(ModuleMsg(QStringLiteral("Aguara"), QStringLiteral("db-connection"),
            QCoreApplication::translate("main", "Conexion restablecida"),
            QCoreApplication::translate("main", "Se recupero la conexion con el servidor de datos."),
            MsgPriority::PriorityLow, 10));
    });

    MainWindow w(&host);
    w.show();

    return a.exec();
}
