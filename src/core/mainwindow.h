#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "inactivitywatcher.h"
#include "interfaces.h"
#include "loadedmodule.h"
#include "pagecero.h"

#include <QDir>
#include <QHash>
#include <QLabel>
#include <QMainWindow>
#include <QTimer>

class Backup;
class CoreHost;
class NotificationDialog;
class SqlUserService;

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
class QAction;
class QMenu;
class QPushButton;
class QVBoxLayout;
QT_END_NAMESPACE

/*
    The application window:

      sidebar  | page tabs (Avisos + the pages modules open)
               |
      ---------+----------------------------------------------
      status bar: where the data is, connection, user, clock

    The sidebar lists the modules that provide a page (Ctrl+1..9), then
    Avisos (with the number of open notifications) and the module list.
*/
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    /// `backup` may be null (Archivo > Copia de seguridad then says it's unavailable).
    /// `modulesDirectory`: empty is the "modules" folder next to the program.
    explicit MainWindow(CoreHost *host, Backup *backup = nullptr, QWidget *parent = nullptr,
                        const QString &modulesDirectory = QString());
    ~MainWindow();

    /// Turn on users and permissions: registers the permissions of the core
    /// and the modules, hides what the signed-in user can't open, and locks
    /// the session after `lockMinutes` without activity (0: never).
    /// Without it (tests) nothing is restricted.
    void setUsers(SqlUserService *users, int lockMinutes);

    /// false: the data comes from the command line, not from aguara.ini, so
    /// Preferencias > Conexión a los datos would have no effect.
    void setConnectionEditable(bool editable);

    /// Close to start again: whoever changed the connection was authorized.
    void closeForRestart();

signals:
    /// The user saved other connection settings and wants them now.
    void restartRequested();
    /// Someone else may be signed in, or the same person with other permissions.
    void sessionChanged();

protected:
    /// With a cash session open, leaving takes caja.salir_abierta.
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;

public slots:
    /// Status bar: where the data lives ("Este equipo", "Servidor 192.168.1.10").
    /// `server`: show the connection state too.
    void setDataSource(const QString &description, bool server);
    /// Status bar: the server connection is up / down.
    void setConnected(bool connected);

private slots:
    void Import(ImportArticlesInterface *only = nullptr);
    void backupNow();
    void configureConnection();
    void configurePrinter();
    void about();
    void aboutModules();

    void dateTimeUpdate();
    void openModulePage(int moduleIndex);
    void addTab(QWidget* widget = nullptr, QIcon ico = QIcon(), QString label = "");
    void onTabCloseRequested(int indx);
    void updateKeptPages();
    void showNotifications();
    void onModuleNotification(ModuleMsg msg);
    void syncNavigation();
    void lockSession();
    void switchUser();
    void changeOwnPassword();

private:
    void applySession();
    bool canOpen(int moduleIndex) const;
    bool mustStayOpen(int moduleIndex) const;
    bool mustKeepRunning() const;
    bool mayLeave();
    void openKeptPages();
    void closePage(int index);
    void setClosable(QWidget *page, bool closable);
    void markNewNotifications(bool unseen);
    NotificationDialog *notificationCard(const QString &sender, const QString &id);
    void prepareWindow();
    void buildSidebar();
    void buildStatusBar();
    void setUpPageCero();
    void createActions();
    void createMenus();
    void createShortcuts();
    void loadPlugins();
    void addImportMenuEntry(const LoadedModule &module);
    void makeButton(int moduleIndex);
    QPushButton *makeNavButton(const QIcon &icon, const QString &text);
    QIcon moduleIcon(const LoadedModule &module, const QColor &color) const;

    Ui::MainWindow *ui;
    PageCero pageCero;

    CoreHost *host;
    Backup *backup;
    QDir pluginsDir;
    QString modulesDirectory;
    QString lastImportDir;
    QString dataDescription;
    SqlUserService *users = nullptr;
    InactivityWatcher idleWatcher;
    bool locked = false;
    bool leaving = false;
    qint64 sessionUserId = 0;

    QList<LoadedModule> modules;
    QList<QPushButton *> moduleButtons;     // index = module index (nullptr: no page)
    QHash<QWidget *, int> pageModule;       // open page -> module index
    QHash<QWidget *, QWidget *> closeButtons;   // page that must stay open -> its tab's hidden X
    QList<QPointer<NotificationDialog>> notificationWidgets;

    // sidebar
    QVBoxLayout *moduleNav = nullptr;
    QLabel *noModulesLabel = nullptr;
    QPushButton *notificationsButton = nullptr;
    QWidget *sidebarUser = nullptr;
    QLabel *sidebarUserName = nullptr;
    QLabel *sidebarUserRole = nullptr;

    // status bar
    QLabel *dataIcon = nullptr;
    QLabel *dataLabel = nullptr;
    QLabel *connectionLabel = nullptr;
    QLabel *userLabel = nullptr;
    QLabel timeLabel;
    QLabel dateLabel;
    QTimer dateTimeTimer;

    QMenu* fileMenu = nullptr;
    QMenu* importMenu = nullptr;
    QMenu* sessionMenu = nullptr;
    QMenu* preferencesMenu = nullptr;
    QMenu* helpMenu = nullptr;
    QAction* importAct = nullptr;
    QAction* backupAct = nullptr;
    QAction* connectionAct = nullptr;
    QAction* printerAct = nullptr;
    QAction* quitAct = nullptr;
    QAction* aboutAct = nullptr;
    QAction* aboutModulesAct = nullptr;
};
#endif // MAINWINDOW_H
