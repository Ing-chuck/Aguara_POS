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

    // Services modules can use, looked up through the host.
    SqlProductService products(database.connectionName());
    CoreHost host;
    host.registerService(ProductService_iid, &products);

    MainWindow w(&host);
    w.show();

    return a.exec();
}
