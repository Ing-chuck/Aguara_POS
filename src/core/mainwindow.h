#ifndef MAINWINDOW_H
#define MAINWINDOW_H

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
    explicit MainWindow(CoreHost *host, Backup *backup = nullptr, QWidget *parent = nullptr);
    ~MainWindow();

public slots:
    /// Status bar: where the data lives ("Este equipo", "Servidor 192.168.1.10").
    /// `server`: show the connection state too.
    void setDataSource(const QString &description, bool server);
    /// Status bar: the server connection is up / down.
    void setConnected(bool connected);

private slots:
    void Import(ImportArticlesInterface *only = nullptr);
    void backupNow();
    void about();
    void aboutModules();

    void dateTimeUpdate();
    void openModulePage(int moduleIndex);
    void addTab(QWidget* widget = nullptr, QIcon ico = QIcon(), QString label = "");
    void onTabCloseRequested(int indx);
    void onModuleNotification(ModuleMsg msg);
    void syncNavigation();

private:
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
    QString lastImportDir;
    QString dataDescription;

    QList<LoadedModule> modules;
    QList<QPushButton *> moduleButtons;     // index = module index (nullptr: no page)
    QHash<QWidget *, int> pageModule;       // open page -> module index
    QList<QPointer<NotificationDialog>> notificationWidgets;

    // sidebar
    QVBoxLayout *moduleNav = nullptr;
    QLabel *noModulesLabel = nullptr;
    QPushButton *notificationsButton = nullptr;
    QLabel *notificationsBadge = nullptr;

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
    QMenu* helpMenu = nullptr;
    QAction* importAct = nullptr;
    QAction* backupAct = nullptr;
    QAction* quitAct = nullptr;
    QAction* aboutAct = nullptr;
    QAction* aboutModulesAct = nullptr;
};
#endif // MAINWINDOW_H
