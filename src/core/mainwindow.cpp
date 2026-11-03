#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "backup.h"
#include "corehost.h"
#include "logging.h"
#include "ui_pagecero.h"
#include "module_dialog.h"
#include "notificationdialog.h"
#include <version.h>

#include <QAction>
#include <QApplication>
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
#include <QShortcut>
#include <QStandardPaths>



MainWindow::MainWindow(CoreHost *host, Backup *backup, QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , host(host)
    , backup(backup)
{
    ui->setupUi(this);
    ui->dockWidget->setTitleBarWidget(new QWidget);

    prepareWindow();
    setUpPageCero();

    connect(ui->tabWidget, SIGNAL(tabCloseRequested(int)), this, SLOT(onTabCloseRequested(int)));

    createActions();
    createMenus();
    createShortcuts();

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
    addTab(&pageCero, QIcon(), tr("Avisos"));
    ui->tabWidget->tabBar()->setTabButton(0, QTabBar::RightSide, 0);
    ui->tabWidget->tabBar()->setTabButton(0, QTabBar::LeftSide, 0);
}

/// Import articles from a file. With `only` set, offer only that module's
/// file types; otherwise every importer's, and the file's extension picks
/// the module.
void MainWindow::Import(ImportArticlesInterface *only)
{
    QStringList patterns;
    for (const LoadedModule &module : qAsConst(modules)) {
        if (module.importer && (!only || module.importer == only)) {
            for (const QString &type : module.importer->fileTypes())
                patterns << QStringLiteral("*.") + type;
        }
    }
    if (patterns.isEmpty()) {
        QMessageBox::information(this, tr("Importar artículos"),
                                 tr("No hay ningún módulo de importación instalado."));
        return;
    }
    patterns.removeDuplicates();

    if (lastImportDir.isEmpty())
        lastImportDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString file = QFileDialog::getOpenFileName(this, tr("Importar artículos"), lastImportDir,
        tr("Archivos de artículos (%1)").arg(patterns.join(QLatin1Char(' '))));
    if (file.isEmpty())
        return;
    lastImportDir = QFileInfo(file).absolutePath();

    const QString extension = QFileInfo(file).suffix().toLower();
    for (const LoadedModule &module : qAsConst(modules)) {
        if (!module.importer || (only && module.importer != only)
            || !module.importer->fileTypes().contains(extension))
            continue;
        qCInfo(lcModules) << "Importing" << file << "with" << module.info.name;
        module.importer->importFrom(file);     // the module reports the result as a notification
        return;
    }
    QMessageBox::warning(this, tr("Importar artículos"),
                         tr("Ningún módulo puede importar archivos .%1.").arg(extension));
}

/// Archivo > Copia de seguridad...
void MainWindow::backupNow()
{
    if (!backup || !backup->isAvailable()) {
        QMessageBox::information(this, tr("Copia de seguridad"),
            tr("Los datos están en el servidor de la red: la copia de seguridad se hace en el servidor "
               "(ver la guía de instalación de varios puestos)."));
        return;
    }
    const QString suggested = QDir(Backup::defaultDirectory()).filePath(Backup::fileNameFor(QDateTime::currentDateTime()));
    const QString file = QFileDialog::getSaveFileName(this, tr("Guardar copia de seguridad"), suggested,
                                                      tr("Copia de AguaraPOS (*.db)"));
    if (file.isEmpty())
        return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = backup->backupTo(file);
    QApplication::restoreOverrideCursor();
    if (ok)
        QMessageBox::information(this, tr("Copia de seguridad"),
                                 tr("Copia guardada en:\n%1").arg(QDir::toNativeSeparators(file)));
    else
        QMessageBox::warning(this, tr("Copia de seguridad"), backup->lastError());
}

void MainWindow::about()
{
    QMessageBox::about(this, tr("Acerca de AguaraPOS"),
        tr("<b>AguaraPOS %1</b><br>Sistema de punto de venta.<br><br>"
           "Registro del programa:<br>%2")
            .arg(QStringLiteral(VERSION_STRING),
                 QDir::toNativeSeparators(AguaraLog::filePath()).toHtmlEscaped()));
}

void MainWindow::aboutModules()
{
    ModuleDialog dialog(pluginsDir.path(), modules, this);
    dialog.exec();
}

void MainWindow::createActions()
{
    // Ctrl+I, not Ctrl+N: "new" belongs to the open page (e.g. a new article).
    importAct = new QAction(tr("&Importar artículos..."), this);
    importAct->setObjectName(QStringLiteral("importAction"));
    importAct->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_I));
    importAct->setEnabled(false);               // until an import module loads
    connect(importAct, &QAction::triggered, this, [this]() { Import(); });

    backupAct = new QAction(tr("&Copia de seguridad..."), this);
    backupAct->setObjectName(QStringLiteral("backupAction"));
    connect(backupAct, &QAction::triggered, this, &MainWindow::backupNow);

    quitAct = new QAction(tr("&Salir"), this);
    quitAct->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_Q));
    connect(quitAct, &QAction::triggered, this, &QWidget::close);

    aboutAct = new QAction(tr("&Acerca de AguaraPOS"), this);
    connect(aboutAct, &QAction::triggered, this, &MainWindow::about);

    aboutModulesAct = new QAction(tr("&Módulos instalados..."), this);
    connect(aboutModulesAct, &QAction::triggered, this, &MainWindow::aboutModules);
}

void MainWindow::createMenus()
{
    // essential module independant menus
    fileMenu = menuBar()->addMenu(tr("&Archivo"));
    fileMenu->addAction(importAct);
    fileMenu->addAction(backupAct);
    fileMenu->addSeparator();
    fileMenu->addAction(quitAct);

    helpMenu = menuBar()->addMenu(tr("A&yuda"));
    helpMenu->addAction(aboutAct);
    helpMenu->addAction(aboutModulesAct);
}

/// Keyboard: Ctrl+1..9 open the modules in sidebar order, Ctrl+W closes the
/// page, Ctrl+Tab / Ctrl+Shift+Tab move between pages, F1 shows the
/// notifications page.
void MainWindow::createShortcuts()
{
    for (int n = 1; n <= 9; ++n) {
        auto shortcut = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_0 + n), this);
        connect(shortcut, &QShortcut::activated, this, [this, n]() {
            int seen = 0;
            for (int i = 0; i < modules.size(); ++i) {
                if (modules.at(i).widgets && ++seen == n) {
                    openModulePage(i);
                    return;
                }
            }
        });
    }
    auto closePage = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_W), this);
    connect(closePage, &QShortcut::activated, this, [this]() {
        onTabCloseRequested(ui->tabWidget->currentIndex());
    });
    auto next = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_Tab), this);
    connect(next, &QShortcut::activated, this, [this]() {
        const int count = ui->tabWidget->count();
        if (count > 1)
            ui->tabWidget->setCurrentIndex((ui->tabWidget->currentIndex() + 1) % count);
    });
    auto previous = new QShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_Backtab), this);
    connect(previous, &QShortcut::activated, this, [this]() {
        const int count = ui->tabWidget->count();
        if (count > 1)
            ui->tabWidget->setCurrentIndex((ui->tabWidget->currentIndex() + count - 1) % count);
    });
    auto notifications = new QShortcut(QKeySequence(Qt::Key_F1), this);
    connect(notifications, &QShortcut::activated, this, [this]() { addTab(&pageCero); });
}

/// Load every module from the "modules" folder next to the executable.
void MainWindow::loadPlugins()
{
    pluginsDir = QDir(QCoreApplication::applicationDirPath());
    if (!pluginsDir.cd("modules")) {
        qCWarning(lcModules) << "Modules folder not found:" << pluginsDir.absoluteFilePath("modules");
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
            qCWarning(lcModules) << "Skipping" << fileName << "- interface" << iid
                                 << "expected" << AguaraModuleInterface_iid;
            onModuleNotification(ModuleMsg("Aguara", "module-skipped:" + fileName,
                tr("Módulo no cargado"),
                tr("%1 es para otra versión de AguaraPOS y no se cargó.").arg(fileName),
                MsgPriority::PriorityHigh));
            continue;
        }

        QObject *plugin = loader.instance();
        auto module = qobject_cast<AguaraModuleInterface *>(plugin);
        if (!module) {
            qCWarning(lcModules) << "Could not load" << fileName << ":" << loader.errorString();
            loader.unload();
            continue;
        }

        if (!module->initialize(host)) {
            qCWarning(lcModules) << "Module" << fileName << "failed to initialize";
            onModuleNotification(ModuleMsg("Aguara", "module-init-failed:" + fileName,
                tr("Módulo no cargado"),
                tr("%1 no pudo iniciar: le falta un servicio del programa.").arg(module->moduleInfo().name),
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
        qCInfo(lcModules) << "Loaded module" << fileName << loaded.info.id << loaded.info.version;

        if (loaded.importer)
            addImportMenuEntry(modules.last());
        if (loaded.widgets)
            makeButton(modules.size() - 1);
    }
}

/// Add an "Import from" menu entry for a module that can import articles.
void MainWindow::addImportMenuEntry(const LoadedModule &module)
{
    importAct->setEnabled(true);

    // if this is the first import module, add the submenu
    if (importMenu == nullptr) {
        importMenu = new QMenu(tr("Importar &desde"), this);
        fileMenu->insertMenu(backupAct, importMenu);
    }

    const QString text = QString("%1 (%2)").arg(module.info.name,
                                                module.importer->fileTypes().join(", "));
    auto action = new QAction(module.info.icon, text, this);
    ImportArticlesInterface *importer = module.importer;
    connect(action, &QAction::triggered, this, [this, importer]() { Import(importer); });
    importMenu->addAction(action);
}

/// Show a notification. Sender + id identify a message (see messages.h):
/// a message with the same pair updates the card already shown instead of
/// adding another one.
void MainWindow::onModuleNotification(ModuleMsg msg) {
    notificationWidgets.removeAll(QPointer<NotificationDialog>());     // dismissed/expired

    const QString key = msg.senderName + QLatin1Char('\n') + msg.id;
    NotificationDialog *nd = nullptr;
    for (const QPointer<NotificationDialog> &existing : qAsConst(notificationWidgets)) {
        if (existing->property("aguaraKey").toString() == key) {
            nd = existing;
            break;
        }
    }

    if (!nd) {
        nd = new NotificationDialog();
        nd->setProperty("aguaraKey", key);
        pageCero.addWidget(nd);
        notificationWidgets.append(nd);
    }
    nd->setMessage(msg);
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
    if (moduleButtonList.size() <= 9)
        button->setToolTip(tr("%1 (Ctrl+%2)").arg(module.info.name).arg(moduleButtonList.size()));
    addToSidebar(button);

    connect(act, &QAction::triggered, this, [this, moduleIndex]() { openModulePage(moduleIndex); });
}

void MainWindow::addToSidebar(QToolButton *button) {
    ui->sideBar->layout()->addWidget(button);
}

void MainWindow::dateTimeUpdate() {
    QDateTime current_time = QDateTime::currentDateTime();
    QString datestr = current_time.toString( "dd/MM/yyyy"); //Set the format of the display
    // 24-hour "21:05": two-digit minutes, and no AM/PM text, which comes from
    // the system locale and is empty on some Spanish Windows installs
    QString timestr = current_time.toString( "HH:mm");
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
