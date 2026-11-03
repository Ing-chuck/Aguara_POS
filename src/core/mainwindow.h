#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "interfaces.h"
#include "loadedmodule.h"
#include "pagecero.h"

#include <QMainWindow>
#include <QDir>
#include <QLabel>
#include <QShortcut>
#include <QTimer>

class CoreHost;
class NotificationDialog;

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; /*class PageCero;*/ }
class QAction;
class QMenu;
class QToolButton;
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(CoreHost *host, QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void Import();
    void about();
    void aboutModules();

    void dateTimeUpdate();
    void openModulePage(int moduleIndex);
    void addTab(QWidget* widget = nullptr, QIcon ico = QIcon(), QString label = "");
    void onTabCloseRequested(int indx);
    void onModuleNotification(ModuleMsg msg);

private:
    void prepareWindow();
    void setUpPageCero();
    void createActions();
    void createMenus();
    void loadPlugins();
    void addImportMenuEntry(const LoadedModule &module);
    void makeButton(int moduleIndex);
    void addToSidebar(QToolButton *button);

    QLabel userLabel;
    QLabel blankLabel;
    QLabel dateLabel;
    QLabel timeLabel;
    QTimer dateTimeTimer;

    Ui::MainWindow *ui;
    //Ui::PageCero *ui2;

    PageCero pageCero;

    CoreHost *host;
    QDir pluginsDir;

    QList<LoadedModule> modules;

    //QShortcut newTabShortcut;
    QMenu* importMenu = nullptr;
    QMenu* helpMenu = nullptr;
    QAction* importAct = nullptr;
    QAction* aboutAct = nullptr;
    QAction* aboutModulesAct = nullptr;

    QList<QToolButton*> moduleButtonList;
    QList<QPointer<NotificationDialog>> notificationWidgets;
};
#endif // MAINWINDOW_H
