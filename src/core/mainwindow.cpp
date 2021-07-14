#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "version.h"
#include "interfaces.h"
#include "module_dialog.h"

#include <QAction>
#include <QActionGroup>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QTimer>
#include <QTableView>
#include <QFileDialog>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setWindowTitle("AguaraPOS " VERSION_STRING);

    createActions();
    createMenus();
    loadPlugins();
    loadTable();

    //QTimer::singleShot(500, this, &MainWindow::aboutModules);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::loadTable() {
    // Create the data model:
    model = new QSqlTableModel(ui->tableView);
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
    ui->tableView->setModel(model);
    //ui->tableView->setItemDelegate(new BookDelegate(ui.bookTable));
    //ui->tableView->setColumnHidden(model->fieldIndex("id"), true);
    ui->tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->tableView->resizeColumnsToContents();
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
        loadTable();

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
    importMenu = menuBar()->addMenu(tr("&Import"));
    importMenu->addAction(importAct);

    menuBar()->addSeparator();

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

    const auto entryList = pluginsDir.entryList(QDir::Files);
    for (const QString &fileName : entryList) {
        QPluginLoader loader(pluginsDir.absoluteFilePath(fileName));
        QObject *plugin = loader.instance();
        if (plugin && dynamic_cast<AguaraModule *>(plugin)->registerModule(VERSION_STRING)) {
            populateMenus(plugin);
            pluginFileNames += fileName;
        }
    }
    importMenu->setEnabled(!importMenu->actions().isEmpty());
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
