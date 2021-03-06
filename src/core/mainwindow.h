#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "interfaces.h"
#include "pagecero.h"

#include <QMainWindow>
#include <QtSql>
#include <QDir>
#include <QLabel>
#include <QShortcut>
#include <QTimer>

class NotificationDialog;

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; /*class PageCero;*/ }
class QAction;
class QActionGroup;
class QMenu;
class QTableView;
class QToolButton;
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void Import();
    void about();
    void aboutModules();

    void dateTimeUpdate();
    void addTabFromAction(QAction* act);
    void addTab(QWidget* widget = nullptr, QIcon ico = QIcon(), QString label = "");
    void onTabCloseRequested(int indx);
    void closeAllTabs();
    void onModuleNotification(ModuleMsg msg);

private:
    typedef void (MainWindow::*Member)();

    void prepareWindow();
    void setUpPageCero();
    void loadTable(QTableView& tableView);
    void createActions();
    void createMenus();
    void loadPlugins();
    void populateMenus(QObject *plugin);
    void addToMenu(QObject *plugin, const QStringList &texts, QMenu *menu,
                   Member member, QActionGroup *actionGroup = nullptr);
    void makeButton(QObject *plugin);
    void addToSidebar(QToolButton *button);

    QLabel userLabel;
    QLabel blankLabel;
    QLabel dateLabel;
    QLabel timeLabel;
    QTimer dateTimeTimer;

    Ui::MainWindow *ui;
    //Ui::PageCero *ui2;

    PageCero pageCero;

    QSqlTableModel *model;
    QDir pluginsDir;
    QStringList pluginFileNames;

    QList<QObject*> attachedModules;
    QList<QObject*> attachedImportModules;

    //QShortcut newTabShortcut;
    QMenu* importMenu = nullptr;
    QMenu* helpMenu = nullptr;
    QAction* importAct = nullptr;
    QAction* aboutAct = nullptr;
    QAction* aboutModulesAct = nullptr;

    QList<QSharedPointer<QWidget>> tabPageWidgets;
    QList<QToolButton*> moduleButtonList;
    QList<QPointer<NotificationDialog>> notificationWidgets;
};
#endif // MAINWINDOW_H
