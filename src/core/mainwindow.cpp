#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "corehost.h"
#include "ui_pagecero.h"
#include "module_dialog.h"
#include "notificationdialog.h"
#include <version.h>

#include <QAction>
#include <QDateTime>
#include <QDebug>
#include <QFileDialog>
#include <QFileInfo>
#include <QLibrary>
#include <QPluginLoader>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QTableView>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QScreen>



MainWindow::MainWindow(CoreHost *host, QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , host(host)
    //, newTabShortcut(QShortcut(QKeySequence(tr("Ctrl+N")), this))
{
    ui->setupUi(this);
    ui->dockWidget->setTitleBarWidget(new QWidget);

    prepareWindow();
    setUpPageCero();

    // set shortcut
    //newTabShortcut.setAutoRepeat(false);
    //connect(&newTabShortcut, SIGNAL(activated()),this,SLOT(addTab()));
    connect(ui->tabWidget, SIGNAL(tabCloseRequested(int)), this, SLOT(onTabCloseRequested(int)));

    createActions();
    createMenus();

    // modules without a page (e.g. importers) notify through the host
    connect(host, &CoreHost::notificationRequested, this, &MainWindow::onModuleNotification);
    loadPlugins();
    //loadTable();

    //QTimer::singleShot(500, this, &MainWindow::aboutModules);
}

MainWindow::~MainWindow()
{
    // Delete module pages while the window is still fully alive, and stop
    // listening to them first: their destructors may still emit notify().
    for (int i = ui->tabWidget->count() - 1; i >= 0; --i) {
        QWidget *page = ui->tabWidget->widget(i);
        if (page == &pageCero)
            continue;
        disconnect(page, nullptr, this, nullptr);
        delete page;
    }
    delete ui;
}

// setup all window flags and other properties
void MainWindow::prepareWindow() {
    setWindowTitle("AguaraPOS " VERSION_STRING);

    // set window flags
    //this->setWindowFlags(Qt::FramelessWindowHint);

    // make window fullscreen
    QMainWindow::showMaximized();
    ui->centralwidget->showMaximized();
    ui->statusbar->setSizeGripEnabled(false);
    //ui->sideBar->setHidden(true);

    // set status bar labels
    userLabel.setText("user: x");

    // add labels to status bar
    ui->statusbar->addWidget(&blankLabel);
    ui->statusbar->addWidget(&userLabel);
    ui->statusbar->addPermanentWidget(&timeLabel);
    ui->statusbar->addPermanentWidget(&dateLabel);
    dateTimeUpdate();

    //set the timer to update the date and time
    dateTimeTimer.start(1000); //Send the signal of timeout every 1000ms
    connect(&dateTimeTimer, SIGNAL(timeout()),this,SLOT(dateTimeUpdate()));
}

void MainWindow::setUpPageCero() {
    addTab(&pageCero, QIcon(), "Notifications");
    ui->tabWidget->tabBar()->setTabButton(0, QTabBar::RightSide, 0);
    ui->tabWidget->tabBar()->setTabButton(0, QTabBar::LeftSide, 0);
}

void MainWindow::Import() {
    qDebug() << "importing";

    QString file = QFileDialog::getOpenFileName(this, "Select file to import", QDir::currentPath(), "CSV Files (*.csv)");
    if (file.isEmpty())
        return;

    const QString file_ext = QFileInfo(file).suffix().toLower();

    //search for module that can handle this type
    for (const LoadedModule &module : qAsConst(modules)) {
        if (!module.importer || !module.importer->fileTypes().contains(file_ext))
            continue;

        //let this module handle the file
        module.importer->importFrom(file);
        //loadTable();

        break;
    }
}

void MainWindow::about()
{
   QMessageBox::about(this, tr("About AguaraPOS"),
            tr("TODO"));
}

void MainWindow::aboutModules()
{
    ModuleDialog dialog(pluginsDir.path(), modules, this);
    dialog.exec();
}

void MainWindow::createActions()
{
    importAct = new QAction(tr("&Import"), this);
    importAct->setShortcuts(QKeySequence::New);
    connect(importAct, &QAction::triggered, this, &MainWindow::Import);

    aboutAct = new QAction(tr("&About"), this);
    connect(aboutAct, &QAction::triggered, this, &MainWindow::about);

    aboutModulesAct = new QAction(tr("About &Plugins"), this);
    connect(aboutModulesAct, &QAction::triggered, this, &MainWindow::aboutModules);
}

void MainWindow::createMenus()
{
    // essential module independant menus

    helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(aboutAct);
    helpMenu->addAction(aboutModulesAct);
}

/// Load every module from the "modules" folder next to the executable.
void MainWindow::loadPlugins()
{
    pluginsDir = QDir(QCoreApplication::applicationDirPath());
    if (!pluginsDir.cd("modules")) {
        qWarning() << "Modules folder not found:" << pluginsDir.absoluteFilePath("modules");
        return;
    }

    const QStringList entryList = pluginsDir.entryList(QDir::Files);
    for (const QString &fileName : entryList) {
        if (!QLibrary::isLibrary(fileName))     // skips import libraries (.a) etc.
            continue;

        QPluginLoader loader(pluginsDir.absoluteFilePath(fileName));

        // Check the interface version from the metadata before loading any
        // code: a module built against another interface version is skipped.
        const QString iid = loader.metaData().value("IID").toString();
        if (iid != QLatin1String(AguaraModuleInterface_iid)) {
            qWarning() << "Skipping" << fileName << "- interface" << iid
                       << "expected" << AguaraModuleInterface_iid;
            onModuleNotification(ModuleMsg("Aguara", "module-skipped:" + fileName,
                tr("Module not loaded"),
                tr("%1 was built for a different version of Aguara and was not loaded.").arg(fileName),
                MsgPriority::PriorityHigh));
            continue;
        }

        QObject *plugin = loader.instance();
        auto module = qobject_cast<AguaraModuleInterface *>(plugin);
        if (!module) {
            qWarning() << "Could not load" << fileName << ":" << loader.errorString();
            loader.unload();
            continue;
        }

        if (!module->initialize(host)) {
            qWarning() << "Module" << fileName << "failed to initialize";
            onModuleNotification(ModuleMsg("Aguara", "module-init-failed:" + fileName,
                tr("Module not loaded"),
                tr("%1 could not start: a service it needs is missing.").arg(module->moduleInfo().name),
                MsgPriority::PriorityHigh));
            continue;
        }

        LoadedModule loaded;
        loaded.fileName = fileName;
        loaded.instance = plugin;
        loaded.info     = module->moduleInfo();
        loaded.widgets  = qobject_cast<WidgetModuleInterface *>(plugin);
        loaded.importer = qobject_cast<ImportArticlesInterface *>(plugin);
        modules.append(loaded);

        if (loaded.importer)
            addImportMenuEntry(modules.last());
        if (loaded.widgets)
            makeButton(modules.size() - 1);
    }
}

/// Add an "Import from" menu entry for a module that can import articles.
void MainWindow::addImportMenuEntry(const LoadedModule &module)
{
    // if this is the first import module, add the menu
    if (importMenu == nullptr)
        importMenu = menuBar()->addMenu(tr("&Import from"));

    const QString text = QString("%1 (%2)").arg(module.info.name,
                                                module.importer->fileTypes().join(", "));
    auto action = new QAction(module.info.icon, text, this);
    connect(action, &QAction::triggered, this, &MainWindow::Import);
    importMenu->addAction(action);
}

void MainWindow::onModuleNotification(ModuleMsg msg) {
    QPointer<NotificationDialog> nd = new NotificationDialog();
    nd->setTitle(msg.title);
    nd->setMsg(msg.message);
    nd->setPriority(msg.priority);

    pageCero.addWidget(nd);
    notificationWidgets.append(nd);
}

/// Add a sidebar button that opens the module's page.
void MainWindow::makeButton(int moduleIndex) {
    const LoadedModule &module = modules.at(moduleIndex);

    QToolButton *button = new QToolButton(ui->sideBar);
    QAction *act = new QAction(module.info.icon, module.info.name, button);
    act->setIconVisibleInMenu(false);
    button->setDefaultAction(act);
    button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    moduleButtonList.append(button);
    addToSidebar(button);

    connect(act, &QAction::triggered, this, [this, moduleIndex]() { openModulePage(moduleIndex); });
}

void MainWindow::addToSidebar(QToolButton *button) {
    ui->sideBar->layout()->addWidget(button);
}

void MainWindow::dateTimeUpdate() {
    QDateTime current_time = QDateTime::currentDateTime();
    QString datestr = current_time.toString( "dd/MM/yyyy"); //Set the format of the display
    QString timestr = current_time.toString( "h:m A");
    dateLabel.setText(datestr); //Set the text content of the label as time
    timeLabel.setText(timestr);
}

/// Show a module's page: re-focus the open one for single-instance modules,
/// otherwise create a new page. The tab widget owns the page from here on.
void MainWindow::openModulePage(int moduleIndex) {
    LoadedModule &module = modules[moduleIndex];
    const bool multi = module.widgets->allowsMultipleInstances();

    if (!multi && module.openPage) {
        addTab(module.openPage);    // already a tab: just make it current
        return;
    }

    QWidget *page = module.widgets->createWidget();
    if (!page)
        return;

    if (auto ag = qobject_cast<AguaraWidget *>(page)) {
        const QString sender = module.info.name;
        connect(ag, &AguaraWidget::notify, this, [this, sender](ModuleMsg msg) {
            if (msg.senderName.isEmpty())
                msg.senderName = sender;
            onModuleNotification(msg);
        });
    }

    if (!multi)
        module.openPage = page;
    addTab(page, module.info.icon, module.info.name);
}

void MainWindow::addTab(QWidget* widget, QIcon ico, QString label) {

    QWidget* page = (widget)? widget : new QWidget();
    int pageN = ui->tabWidget->count();
    label = (label != "")? label : QStringLiteral("tab %1").arg(pageN);
    int ind;

    // check if this widget is already a tab
    bool newPage = true;
    for(ind = 0; ind < ui->tabWidget->count(); ind++) {
        if(page == ui->tabWidget->widget(ind)){
            newPage = false;
            break;
        }
    }

    if(newPage)
        ui->tabWidget->addTab(page, ico, label);

    // set tab as active
    ui->tabWidget->setCurrentIndex(ind);
}

void MainWindow::onTabCloseRequested(int indx) {
    QWidget* page = ui->tabWidget->widget(indx);
    if (page == &pageCero)      // the notifications page is permanent
        return;

    ui->tabWidget->removeTab(indx);
    for (LoadedModule &module : modules) {
        if (module.openPage == page)
            module.openPage = nullptr;  // don't hand out a page about to be deleted
    }
    page->deleteLater();
}
