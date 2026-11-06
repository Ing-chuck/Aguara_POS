#include "activitylog.h"
#include "backup.h"
#include "connectionmonitor.h"
#include "corehost.h"
#include "crashhandler.h"
#include "database.h"
#include "databasesettingsdialog.h"
#include "logging.h"
#include "lowstockwatcher.h"
#include "mainwindow.h"
#include "sessiondialogs.h"
#include "sqlcashservice.h"
#include "sqlcustomerservice.h"
#include "sqlinventoryservice.h"
#include "sqlprintservice.h"
#include "sqlproductservice.h"
#include "sqlreportservice.h"
#include "sqlreturnservice.h"
#include "sqlsalesservice.h"
#include "sqluserservice.h"
#include "telemetry.h"
#include <aguarastyle.h>
#include <apiversion.h>
#include <version.h>

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QLibraryInfo>
#include <QLocale>
#include <QProcess>
#include <QSettings>
#include <QSysInfo>
#include <QTranslator>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    // Lets a patch see that the program is running (AppMutex in the installer).
    // Windows releases it when the program ends.
    CreateMutexW(nullptr, FALSE, L"AguaraPOS-running");
#endif

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
    qCInfo(lcApp).noquote() << "AguaraPOS" << VERSION_STRING << "API" << AGUARA_API_VERSION
                            << "starting | Qt" << qVersion()
                            << "|" << QSysInfo::prettyProductName() << QSysInfo::currentCpuArchitecture();

    // Spanish everywhere: Qt's own texts (Sí/No/Cancelar, file dialogs, the
    // calendar) and number/date formats, whatever the Windows language.
    QLocale::setDefault(QLocale(QLocale::Spanish, QLocale::Paraguay));
    QTranslator qtTranslator;
    const QString qtTranslations = QLibraryInfo::location(QLibraryInfo::TranslationsPath);
    if (qtTranslator.load(QStringLiteral("qtbase_es"), qtTranslations)
        || qtTranslator.load(QStringLiteral("qtbase_es"), QCoreApplication::applicationDirPath() + QStringLiteral("/translations"))
        || qtTranslator.load(QStringLiteral("qt_es"), QCoreApplication::applicationDirPath() + QStringLiteral("/translations")))     // as windeployqt names it
        a.installTranslator(&qtTranslator);
    else
        qCWarning(lcApp) << "Qt Spanish translation (qtbase_es.qm) not found: standard buttons stay in English";

    // Always on, and only in this computer: what happened, and what went wrong
    const QString crashes = QDir(AguaraLog::defaultDirectory()).filePath(QStringLiteral("crashes"));
    QDir().mkpath(crashes);
    CrashHandler::install(crashes, QString::fromLatin1(VERSION_STRING).mid(1));
    ActivityLog::open(AguaraLog::defaultDirectory());
    if (ActivityLog::lastRunEndedBadly()) {
        qCWarning(lcApp) << "The last run didn't end normally: a crash, a forced close or a power cut";
        ActivityLog::record(QStringLiteral("programa.cierre_inesperado"));
    }
    ActivityLog::record(QStringLiteral("programa.inicio"));

    // Off until an administrator turns it on
    Telemetry telemetry(Telemetry::defaultDirectory(), crashes);
    telemetry.configure(TelemetrySettings::load());

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
    SqlCashService cash(&connection);
    cash.setUsers(&users);
    SqlSalesService sales(&connection, &products, &cash);
    sales.setUsers(&users);
    SqlPrintService print(&connection, &sales, &cash);
    print.setUsers(&users);
    print.storeInfo();          // remembered: a ticket must print during an outage
    sales.paymentMethods();     // the same: the payment screen offers them
    SqlCustomerService customers(&connection);
    customers.setUsers(&users);
    SqlReturnService returns(&connection, &sales, &cash);
    returns.setUsers(&users);
    SqlReportService reports(&connection, &products);
    reports.setUsers(&users);
    CoreHost host;
    host.registerService(ProductService_iid, &products);
    host.registerService(InventoryService_iid, &inventory);
    host.registerService(UserService_iid, &users);
    host.registerService(CashService_iid, &cash);
    host.registerService(SalesService_iid, &sales);
    host.registerService(PrintService_iid, &print);
    host.registerService(CustomerService_iid, &customers);
    host.registerService(ReturnService_iid, &returns);
    host.registerService(ReportService_iid, &reports);

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

    // What a crash or a lost connection left on this computer's disk
    if (!sales.flushPending())
        qCWarning(lcApp) << "Sales still in the queue:" << sales.pendingSales().size();

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
    w.setConnectionEditable(!explicitFile);
    w.setTelemetry(&telemetry, server ? QStringLiteral("postgresql") : QStringLiteral("sqlite"));
    bool restart = false;
    QObject::connect(&w, &MainWindow::restartRequested, &w, [&restart, &w]() {
        restart = true;
        w.closeForRestart();
    });
    QObject::connect(&connection, &ConnectionMonitor::connectionLost, &w, [&w]() { w.setConnected(false); });
    QObject::connect(&connection, &ConnectionMonitor::reconnected, &w, [&w]() { w.setConnected(true); });
    w.show();

    // Who restocks should know, from the start and as it happens
    LowStockWatcher lowStock(&products, &inventory, &users, &host);
    QObject::connect(&w, &MainWindow::sessionChanged, &lowStock, &LowStockWatcher::check);
    lowStock.check();

    if (!backedUp) {    // after the window exists: it shows the notification
        host.notify(ModuleMsg(QStringLiteral("Aguara"), QStringLiteral("backup"),
            QCoreApplication::translate("main", "Copia de seguridad"),
            QCoreApplication::translate("main", "No se pudo hacer la copia de seguridad automática: %1")
                .arg(backup.lastError()),
            MsgPriority::PriorityHigh));
    }

    const int result = a.exec();
    users.logout();
    ActivityLog::record(QStringLiteral("programa.fin"));
    ActivityLog::close();
    qCInfo(lcApp) << "Exiting, code" << result;
    if (restart) {
        QStringList arguments = QCoreApplication::arguments().mid(1);
        arguments.removeAll(QStringLiteral("--configure-database"));
        if (!QProcess::startDetached(QCoreApplication::applicationFilePath(), arguments))
            qCWarning(lcApp) << "Could not restart";
    }
    return result;
}
