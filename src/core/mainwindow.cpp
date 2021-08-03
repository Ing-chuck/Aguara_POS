#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "interfaces.h"
#include "module_dialog.h"
#include <version.h>

#include <QAction>
#include <QActionGroup>
#include <QDateTime>
#include <QFileDialog>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QTableView>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QScreen>



MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , newTabShortcut(QShortcut(QKeySequence(tr("Ctrl+N")), this))
{
    ui->setupUi(this);

    prepareWindow();

    // set shortcut
    newTabShortcut.setAutoRepeat(false);
    connect(&newTabShortcut, SIGNAL(activated()),this,SLOT(addTab()));
    connect(ui->tabWidget, SIGNAL(tabCloseRequested(int)), this, SLOT(onTabCloseRequested(int)));

    createActions();
    createMenus();
    loadPlugins();
    //loadTable();

    //QTimer::singleShot(500, this, &MainWindow::aboutModules);
}

MainWindow::~MainWindow()
{
    closeAllTabs();
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

void MainWindow::loadTable(QTableView &tableView) {
    // Create the data model:
    model = new QSqlTableModel(&tableView);
    model->setEditStrategy(QSqlTableModel::OnManualSubmit);
    model->setTable("articles");

    // Set the localized header captions:
    model->setHeaderData(0, Qt::Horizontal, tr("Product Code"));
    model->setHeaderData(1, Qt::Horizontal, tr("Item Name"));
    model->setHeaderData(2, Qt::Horizontal, tr("Brand"));
    model->setHeaderData(3, Qt::Horizontal, tr("Category"));
    model->setHeaderData(4, Qt::Horizontal, tr("Sell Price"));
    model->setHeaderData(5, Qt::Horizontal, tr("Buy Price"));
    model->setHeaderData(6, Qt::Horizontal, tr("Stock"));
    model->setHeaderData(7, Qt::Horizontal, tr("Minimum Stock"));
    model->setHeaderData(8, Qt::Horizontal, tr("InteralImp"));
    model->setHeaderData(9, Qt::Horizontal, tr("VAT"));
    model->setHeaderData(10, Qt::Horizontal, tr("Buy Date"));
    model->setHeaderData(11, Qt::Horizontal, tr("Modified Date"));

    // Populate the model:
    model->select();

    // Set the model and hide the ID column:
    tableView.setModel(model);
    //ui->tableView->setItemDelegate(new BookDelegate(ui.bookTable));
    //ui->tableView->setColumnHidden(model->fieldIndex("id"), true);
    tableView.setSelectionMode(QAbstractItemView::SingleSelection);
    tableView.resizeColumnsToContents();
}

void MainWindow::Import() {
    qDebug() << "importing";

    QString file = QFileDialog::getOpenFileName(this, "Select file to import", QDir::currentPath(), "CSV Files (*.csv)");

    //naively determine file type
    QString file_ext = file.split(".").last();

    //search for module that can handle this type
    qDebug() << "List size: " << attachedImportModules.size();
    for(QObject* module : attachedImportModules){

        QStringList typeList = qobject_cast<ImportArticlesInterface *>(module)->FileTypes();
        qDebug() << "types: " << typeList;
        if(!typeList.contains(file_ext))
            continue;

        //let this module handle the file
        qobject_cast<ImportArticlesInterface *>(module)->ImportFrom(file);
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
    ModuleDialog dialog(pluginsDir.path(), pluginFileNames, this);
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

void MainWindow::loadPlugins()
{
    pluginsDir = QDir(QCoreApplication::applicationDirPath());

#if defined(Q_OS_WIN)
    if (pluginsDir.dirName().toLower() == "debug" || pluginsDir.dirName().toLower() == "release") {
        pluginsDir.cdUp();
        pluginsDir.cdUp();
    }
#endif
    pluginsDir.cd("modules/build-modules");

    const QStringList entryList = pluginsDir.entryList(QDir::Files);
    for (const QString &fileName : entryList) {
        QPluginLoader loader(pluginsDir.absoluteFilePath(fileName));
        QObject *plugin = loader.instance();
        if (plugin && dynamic_cast<AguaraModule *>(plugin)->registerModule(VERSION_STRING)) {
            populateMenus(plugin);
            makeButton(plugin);
            pluginFileNames += fileName;
            attachedModules.append(plugin);
        }
    }
}

void MainWindow::populateMenus(QObject *plugin)
{
    QStringList menuText;
    menuText.append("");

    auto iImport = qobject_cast<ImportArticlesInterface *>(plugin);
    if (iImport){
        //attach
        attachedImportModules.append(plugin);
        QString types = "(";
        for(QString& type : iImport->FileTypes()){
            types += type + ",";
        }
        types = types.left(types.lastIndexOf(",")) + ")";
        menuText[0] += types;

        // if this is the first import interface, add a menu
        if(importMenu == nullptr)
            importMenu = menuBar()->addMenu(tr("&Import from"));

        addToMenu(plugin, menuText, importMenu, &MainWindow::Import);
    }
}

void MainWindow::addToMenu(QObject *plugin, const QStringList &texts,
                           QMenu *menu, Member member,
                           QActionGroup *actionGroup)
{
    for (const QString &text : texts) {
        auto action = new QAction(text, plugin);
        connect(action, &QAction::triggered, this, member);
        menu->addAction(action);

        if (actionGroup) {
            action->setCheckable(true);
            actionGroup->addAction(action);
        }
    }
}

void MainWindow::makeButton(QObject *plugin) {
    auto moduleWidget = qobject_cast<QWidget *>(plugin);
    auto module = dynamic_cast<AmFactory *>(plugin);
    if (module){ 
        QToolButton *button = new QToolButton(moduleWidget);
        QAction *act = new QAction(moduleWidget);
        act->setIcon(module->getIcon());
        act->setIconText(module->getName());
        act->setIconVisibleInMenu(false);
        button->setDefaultAction(act);
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        moduleButtonList.append(button);
        addToSidebar(button);

        //connect button to the tabWidget
        connect(button, &QToolButton::triggered, this, &MainWindow::addTabFromAction);
    }
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

void MainWindow::addTabFromAction(QAction* act) {
    auto singleFactory = qobject_cast<AmSingleInstanceFactory*>(act->parent());
    if(singleFactory){
        // single instace kind
        QSharedPointer<QWidget> module(singleFactory->getInstance());
        if(module){
            tabPageWidgets.append(module);
            addTab(module.data(), act->icon(), act->iconText());
        }
    }
    else {
        // multi instance kind
        auto multiFactory = qobject_cast<AmMultiInstanceFactory*>(act->parent());
        if(multiFactory){
            QSharedPointer<QWidget> module = qobject_cast<QWidget*>(multiFactory->getInstance());
            tabPageWidgets.append(module);
            addTab(module.data(), act->icon(), act->iconText());
        }
    }
}

void MainWindow::addTab(QWidget* widget, QIcon ico, QString label) {
    QWidget* page = (widget)? widget : new QWidget();
    int pageN = ui->tabWidget->count() + 1;
    label = (label != "")? label : QStringLiteral("tab %1").arg(pageN);

    // check if this widget is already a tab
    bool newPage = true;
    if(page == widget){
        for(int i = 0; i < ui->tabWidget->count(); i++) {
            if(page == ui->tabWidget->widget(i)){
                newPage = false;
                break;
            }
        }
    }

    if(newPage)
        ui->tabWidget->addTab(page, ico, label);
}

void MainWindow::onTabCloseRequested(int indx) {
    // search for and remove from our page list
    QWidget* page = ui->tabWidget->widget(indx);
    ui->tabWidget->removeTab(indx);
    this->tabPageWidgets.removeOne(page);
}

void MainWindow::closeAllTabs() {
    for(int i = 0; i < ui->tabWidget->count(); i++) {
        onTabCloseRequested(i);
    }
}
