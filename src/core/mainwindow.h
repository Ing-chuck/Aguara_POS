#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QtSql>
#include <QDir>
#include <QLabel>
#include <QShortcut>
#include <QTimer>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
class QAction;
class QActionGroup;
class QMenu;
class QTableView;
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
    void addTab();
    void onTabCloseRequested(int indx);
    void closeAllTabs();

private:
    typedef void (MainWindow::*Member)();

    void prepareWindow();
    void loadTable(QTableView& tableView);
    void createActions();
    void createMenus();
    void loadPlugins();
    void populateMenus(QObject *plugin);
    void addToMenu(QObject *plugin, const QStringList &texts, QMenu *menu,
                   Member member, QActionGroup *actionGroup = nullptr);

    QLabel userLabel;
    QLabel blankLabel;
    QLabel dateLabel;
    QLabel timeLabel;
    QTimer dateTimeTimer;

    Ui::MainWindow *ui;

    QSqlTableModel *model;
    QDir pluginsDir;
    QStringList pluginFileNames;

    QList<QObject*> attachedImportModules;

    QShortcut newTabShortcut;
    QMenu* importMenu = nullptr;
    QMenu* helpMenu = nullptr;
    QAction* importAct = nullptr;
    QAction* aboutAct = nullptr;
    QAction* aboutModulesAct = nullptr;
};
#endif // MAINWINDOW_H
