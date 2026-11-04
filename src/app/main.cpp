#include "backup.h"
#include "connectionmonitor.h"
#include "corehost.h"
#include "database.h"
#include "databasesettingsdialog.h"
#include "logging.h"
#include "mainwindow.h"
#include "sessiondialogs.h"
#include "sqlinventoryservice.h"
#include "sqlproductservice.h"
#include "sqluserservice.h"
#include <aguarastyle.h>
#include <version.h>

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>
#include <QSysInfo>
#include <QTranslator>

int main(int argc, char *argv[])
{
    // Sharp text and icons at 125 % / 150 % Windows scaling
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication a(argc, argv);
    // Determines the data folder: %APPDATA%\Aguara\AguaraPOS
    QCoreApplication::setOrganizationName(QStringLiteral("Aguara"));
    QCoreApplication::setApplicationName(QStringLiteral("AguaraPOS"));
    QCoreApplication::setApplicationVersion(QStringLiteral(VERSION_STRING));

    // Log file: technical details for support; the user sees short messages.
#ifdef QT_DEBUG
    const bool debugLog = true;
#else
    const bool debugLog = qEnvironmentVariableIntValue("AGUARA_DEBUG") != 0;
#endif
    AguaraLog::install(AguaraLog::defaultDirectory(), debugLog);
    qCInfo(lcApp).noquote() << "AguaraPOS" << VERSION_STRING << "starting | Qt" << qVersion()
                            << "|" << QSysInfo::prettyProductName() << QSysInfo::currentCpuArchitecture();

    // Spanish everywhere: Qt's own texts (Sí/No/Cancelar, file dialogs, the
    // calendar) and number/date formats, whatever the Windows language.
    QLocale::setDefault(QLocale(QLocale::Spanish, QLocale::Paraguay));
    QTranslator qtTranslator;
    const QString qtTranslations = QLibraryInfo::location(QLibraryInfo::TranslationsPath);
    if (qtTranslator.load(QStringLiteral("qtbase_es"), qtTranslations)
        || qtTranslator.load(QStringLiteral("qtbase_es"), QCoreApplication::applicationDirPath() + QStringLiteral("/translations")))
        a.installTranslator(&qtTranslator);
    else
        qCWarning(lcApp) << "Qt Spanish translation (qtbase_es.qm) not found: standard buttons stay in English";

    // The look: before any window (including the database settings dialog)
    AguaraStyle::apply(&a);

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
        if (dialog.exec() != QDialog::Accepted) {
            qCInfo(lcApp) << "Database settings cancelled: exiting";
            return 1;
        }
        settings = dialog.settings();
        if (!explicitFile)
            settings.save();
        showSettings = false;
    }
    qCInfo(lcApp).noquote() << "Data:" << settings.describe();

    // Keeps a PostgreSQL connection alive (reconnects after a server restart
    // or network failure); a no-op for SQLite.
    ConnectionMonitor connection(database.connectionName(),
                                 database.backend() == DatabaseSettings::PostgreSQL);

    // Services modules can use, looked up through the host.
    SqlProductService products(&connection);
    SqlUserService users(&connection);
    users.setSupervisorPrompt(askSupervisor);
    products.setAuditLog(&users);
    SqlInventoryService inventory(&connection, &products);
    inventory.setAuditLog(&users);
    CoreHost host;
    host.registerService(ProductService_iid, &products);
    host.registerService(InventoryService_iid, &inventory);
    host.registerService(UserService_iid, &users);

    // Nothing opens without a user. First run: create the administrator.
    if (users.hasUsers()) {
        LoginDialog login(&users, LoginDialog::Start);
        if (login.exec() != QDialog::Accepted)
            return 0;
    } else {
        FirstAdminDialog firstRun(&users);
        if (firstRun.exec() != QDialog::Accepted)
            return 0;
    }

    // Tell the user when the server is unreachable and when it's back
    // (same sender+id, so the second message replaces the first). The
    // driver's error text is in the log (ConnectionMonitor writes it).
    QObject::connect(&connection, &ConnectionMonitor::connectionLost, &host, [&host](const QString &) {
        host.notify(ModuleMsg(QStringLiteral("Aguara"), QStringLiteral("db-connection"),
            QCoreApplication::translate("main", "Sin conexión con el servidor"),
            QCoreApplication::translate("main", "No se puede acceder a los datos. Verifique que el servidor "
                                                "esté encendido y la red conectada; se reintenta automáticamente."),
            MsgPriority::PriorityVeryHigh));
    });
    QObject::connect(&connection, &ConnectionMonitor::reconnected, &host, [&host]() {
        host.notify(ModuleMsg(QStringLiteral("Aguara"), QStringLiteral("db-connection"),
            QCoreApplication::translate("main", "Conexión restablecida"),
            QCoreApplication::translate("main", "Se recuperó la conexión con el servidor de datos."),
            MsgPriority::PriorityLow, 10));
    });

    // Daily copy of a local (SQLite) database; the server backs up its own.
    Backup backup(&database);
    const bool backedUp = backup.automatic(Backup::defaultDirectory(), 14, 24 * 3600);

    MainWindow w(&host, &backup);
    const bool server = database.backend() == DatabaseSettings::PostgreSQL;
    w.setDataSource(server ? QCoreApplication::translate("main", "Servidor %1").arg(settings.host)
                           : QCoreApplication::translate("main", "Datos en este equipo"), server);
    // [session] lock_minutes in aguara.ini: 0 never locks
    const int lockMinutes = QSettings(DatabaseSettings::settingsPath(), QSettings::IniFormat)
                                .value(QStringLiteral("session/lock_minutes"), 10).toInt();
    w.setUsers(&users, lockMinutes);
    QObject::connect(&connection, &ConnectionMonitor::connectionLost, &w, [&w]() { w.setConnected(false); });
    QObject::connect(&connection, &ConnectionMonitor::reconnected, &w, [&w]() { w.setConnected(true); });
    w.show();

    // Who restocks should know, at the start of the day
    if (users.can(modulePermission(QStringLiteral("inventory")))) {
        const QList<Product> low = inventory.lowStock();
        if (!low.isEmpty()) {
            QStringList names;
            for (int i = 0; i < low.size() && i < 5; ++i)
                names << low.at(i).description;
            host.notify(ModuleMsg(QStringLiteral("Inventario"), QStringLiteral("stock-bajo"),
                low.size() == 1 ? QCoreApplication::translate("main", "1 artículo con stock bajo")
                                : QCoreApplication::translate("main", "%1 artículos con stock bajo").arg(low.size()),
                names.join(QStringLiteral(", ")) + (low.size() > 5 ? QStringLiteral(", ...") : QString()),
                MsgPriority::PriorityHigh));
        }
    }

    if (!backedUp) {    // after the window exists: it shows the notification
        host.notify(ModuleMsg(QStringLiteral("Aguara"), QStringLiteral("backup"),
            QCoreApplication::translate("main", "Copia de seguridad"),
            QCoreApplication::translate("main", "No se pudo hacer la copia de seguridad automática: %1")
                .arg(backup.lastError()),
            MsgPriority::PriorityHigh));
    }

    const int result = a.exec();
    users.logout();
    qCInfo(lcApp) << "Exiting, code" << result;
    return result;
}
