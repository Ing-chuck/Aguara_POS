#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "backup.h"
#include "corehost.h"
#include "logging.h"
#include "module_dialog.h"
#include "notificationdialog.h"
#include <version.h>

#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLibrary>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPluginLoader>
#include <QPushButton>
#include <QShortcut>
#include <QStandardPaths>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>

using AguaraStyle::Color;

namespace {

/// An icon for the dark sidebar: light when idle, white when selected.
QIcon sidebarIcon(const QString &name)
{
    const QIcon idle = AguaraStyle::icon(name, AguaraStyle::color(Color::SidebarText));
    const QIcon active = AguaraStyle::icon(name, QColor(Qt::white));
    QIcon result;
    for (int size : { 16, 20, 24, 32, 40 }) {
        result.addPixmap(idle.pixmap(size, size), QIcon::Normal, QIcon::Off);
        result.addPixmap(active.pixmap(size, size), QIcon::Normal, QIcon::On);
        result.addPixmap(active.pixmap(size, size), QIcon::Active, QIcon::Off);
        result.addPixmap(active.pixmap(size, size), QIcon::Active, QIcon::On);
    }
    return result;
}

} // namespace

MainWindow::MainWindow(CoreHost *host, Backup *backup, QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , host(host)
    , backup(backup)
{
    ui->setupUi(this);

    prepareWindow();
    buildSidebar();
    buildStatusBar();
    setUpPageCero();

    connect(ui->tabWidget, &QTabWidget::tabCloseRequested, this, &MainWindow::onTabCloseRequested);
    connect(ui->tabWidget, &QTabWidget::currentChanged, this, &MainWindow::syncNavigation);

    createActions();
    createMenus();
    createShortcuts();

    // modules without a page (e.g. importers) notify through the host
    connect(host, &CoreHost::notificationRequested, this, &MainWindow::onModuleNotification);
    loadPlugins();
    syncNavigation();
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
    disconnect(&pageCero, nullptr, this, nullptr);
    delete ui;
}

void MainWindow::prepareWindow()
{
    setWindowTitle(QStringLiteral("AguaraPOS"));
    setMinimumSize(1024, 640);      // the smallest common POS screen is 1024x768
    showMaximized();
}

/// Brand, the modules' buttons (added by makeButton), then Avisos and Módulos.
void MainWindow::buildSidebar()
{
    auto layout = qobject_cast<QVBoxLayout *>(ui->sideBar->layout());
    ui->sideBar->setAttribute(Qt::WA_StyledBackground);

    auto brand = new QWidget;
    auto brandLayout = new QHBoxLayout(brand);
    brandLayout->setContentsMargins(18, 18, 14, 18);
    brandLayout->setSpacing(10);
    auto logo = new QLabel;
    logo->setPixmap(AguaraStyle::logo().pixmap(36, 36));
    brandLayout->addWidget(logo);
    auto names = new QVBoxLayout;
    names->setSpacing(0);
    auto brandName = new QLabel(QStringLiteral("AguaraPOS"));
    brandName->setObjectName(QStringLiteral("brandName"));
    auto tagline = new QLabel(tr("Punto de venta"));
    tagline->setObjectName(QStringLiteral("brandTagline"));
    names->addWidget(brandName);
    names->addWidget(tagline);
    brandLayout->addLayout(names);
    brandLayout->addStretch();
    layout->addWidget(brand);

    auto section = new QLabel(tr("MÓDULOS"));
    AguaraStyle::setRole(section, "navSection");
    layout->addWidget(section);

    moduleNav = new QVBoxLayout;
    moduleNav->setSpacing(1);
    layout->addLayout(moduleNav);
    noModulesLabel = new QLabel(tr("Ningún módulo con página instalado."));
    noModulesLabel->setWordWrap(true);
    noModulesLabel->setContentsMargins(18, 4, 18, 4);
    noModulesLabel->setStyleSheet(QStringLiteral("color: %1;").arg(AguaraStyle::color(Color::SidebarMuted).name()));
    moduleNav->addWidget(noModulesLabel);

    layout->addStretch();

    notificationsButton = makeNavButton(sidebarIcon(QStringLiteral("bell")), tr("Avisos"));
    notificationsButton->setObjectName(QStringLiteral("notificationsButton"));
    notificationsButton->setToolTip(tr("Avisos (F1)"));
    auto badgeLayout = new QHBoxLayout(notificationsButton);
    badgeLayout->setContentsMargins(0, 0, 14, 0);
    badgeLayout->addStretch();
    notificationsBadge = new QLabel;
    notificationsBadge->setObjectName(QStringLiteral("notificationsBadge"));
    notificationsBadge->setAlignment(Qt::AlignCenter);
    notificationsBadge->setFixedHeight(18);
    notificationsBadge->setAttribute(Qt::WA_TransparentForMouseEvents);
    AguaraStyle::setRole(notificationsBadge, "badge");
    notificationsBadge->hide();
    badgeLayout->addWidget(notificationsBadge);
    connect(notificationsButton, &QPushButton::clicked, this, [this]() { addTab(&pageCero); syncNavigation(); });
    layout->addWidget(notificationsButton);

    auto modulesButton = makeNavButton(sidebarIcon(QStringLiteral("puzzle")), tr("Módulos instalados"));
    modulesButton->setCheckable(false);
    connect(modulesButton, &QPushButton::clicked, this, &MainWindow::aboutModules);
    layout->addWidget(modulesButton);

    connect(&pageCero, &PageCero::countChanged, this, [this](int count) {
        notificationsBadge->setVisible(count > 0);
        notificationsBadge->setText(count > 99 ? QStringLiteral("99+") : QString::number(count));
    });
}

QPushButton *MainWindow::makeNavButton(const QIcon &icon, const QString &text)
{
    auto button = new QPushButton(icon, text, ui->sideBar);
    AguaraStyle::setRole(button, "nav");
    button->setCheckable(true);
    button->setIconSize(QSize(20, 20));
    button->setCursor(Qt::PointingHandCursor);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    button->setMinimumHeight(40);
    button->setFocusPolicy(Qt::TabFocus);
    return button;
}

void MainWindow::buildStatusBar()
{
    auto data = new QWidget;
    auto dataLayout = new QHBoxLayout(data);
    dataLayout->setContentsMargins(8, 0, 0, 0);
    dataLayout->setSpacing(0);
    dataIcon = new QLabel;
    dataIcon->setPixmap(AguaraStyle::icon(QStringLiteral("database"), AguaraStyle::color(Color::TextMuted)).pixmap(16, 16));
    dataLabel = new QLabel(tr("Sin datos"));
    connectionLabel = new QLabel;
    connectionLabel->setObjectName(QStringLiteral("connectionLabel"));
    connectionLabel->hide();
    dataLayout->addWidget(dataIcon);
    dataLayout->addWidget(dataLabel);
    dataLayout->addWidget(connectionLabel);
    ui->statusbar->addWidget(data);

    // The signed-in user arrives with phase 1 (users and permissions)
    auto user = new QWidget;
    auto userLayout = new QHBoxLayout(user);
    userLayout->setContentsMargins(0, 0, 0, 0);
    userLayout->setSpacing(0);
    auto userIcon = new QLabel;
    userIcon->setPixmap(AguaraStyle::icon(QStringLiteral("user"), AguaraStyle::color(Color::TextMuted)).pixmap(16, 16));
    userLabel = new QLabel(tr("Sin sesión"));
    userLayout->addWidget(userIcon);
    userLayout->addWidget(userLabel);
    ui->statusbar->addPermanentWidget(user);

    timeLabel.setObjectName(QStringLiteral("clock"));
    timeLabel.setStyleSheet(QStringLiteral("font-weight: 600; color: %1;").arg(AguaraStyle::color(Color::Text).name()));
    ui->statusbar->addPermanentWidget(&dateLabel);
    ui->statusbar->addPermanentWidget(&timeLabel);
    dateTimeUpdate();
    connect(&dateTimeTimer, &QTimer::timeout, this, &MainWindow::dateTimeUpdate);
    dateTimeTimer.start(1000);
}

void MainWindow::setDataSource(const QString &description, bool server)
{
    dataDescription = description;
    dataLabel->setText(description);
    dataLabel->setToolTip(description);
    connectionLabel->setVisible(server);
    if (server)
        setConnected(true);
}

void MainWindow::setConnected(bool connected)
{
    const QColor c = AguaraStyle::color(connected ? Color::Success : Color::Danger);
    connectionLabel->setText(QStringLiteral("<span style='color:%1'>&#9679;</span> %2")
                                 .arg(c.name(), connected ? tr("Conectado") : tr("Sin conexión")));
}

void MainWindow::setUpPageCero()
{
    addTab(&pageCero, AguaraStyle::icon(QStringLiteral("bell"), AguaraStyle::color(Color::TextMuted)), tr("Avisos"));
    ui->tabWidget->tabBar()->setTabButton(0, QTabBar::RightSide, nullptr);
    ui->tabWidget->tabBar()->setTabButton(0, QTabBar::LeftSide, nullptr);
    ui->tabWidget->setTabBarAutoHide(true);     // only Avisos open: no tab bar
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
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Acerca de AguaraPOS"));
    auto layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(28, 24, 28, 20);
    layout->setSpacing(16);

    auto top = new QHBoxLayout;
    top->setSpacing(16);
    auto logo = new QLabel;
    logo->setPixmap(AguaraStyle::logo().pixmap(64, 64));
    top->addWidget(logo, 0, Qt::AlignTop);
    auto names = new QVBoxLayout;
    names->setSpacing(2);
    auto title = new QLabel(QStringLiteral("AguaraPOS"));
    AguaraStyle::setRole(title, AguaraStyle::Title);
    auto subtitle = new QLabel(tr("Sistema de punto de venta · versión %1").arg(QStringLiteral(VERSION_STRING)));
    AguaraStyle::setRole(subtitle, AguaraStyle::Subtitle);
    names->addWidget(title);
    names->addWidget(subtitle);
    names->addStretch();
    top->addLayout(names, 1);
    layout->addLayout(top);

    auto details = new QFormLayout;
    details->setHorizontalSpacing(14);
    details->setVerticalSpacing(6);
    auto detail = [&](const QString &label, const QString &value) {
        auto l = new QLabel(label);
        AguaraStyle::setRole(l, AguaraStyle::Muted);
        auto v = new QLabel(value);
        v->setTextInteractionFlags(Qt::TextSelectableByMouse);
        v->setWordWrap(true);
        details->addRow(l, v);
    };
    detail(tr("Datos"), dataDescription.isEmpty() ? tr("(sin datos)") : dataDescription);
    detail(tr("Registro"), QDir::toNativeSeparators(AguaraLog::filePath()));
    detail(tr("Módulos"), QString::number(modules.size()));
    detail(tr("Qt"), QString::fromLatin1(qVersion()));
    layout->addLayout(details);

    auto buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->button(QDialogButtonBox::Close)->setText(tr("Cerrar"));
    buttons->button(QDialogButtonBox::Close)->setDefault(true);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.setMinimumWidth(520);
    dialog.exec();
}

void MainWindow::aboutModules()
{
    ModuleDialog dialog(pluginsDir.path(), modules, this);
    dialog.exec();
}

void MainWindow::createActions()
{
    // Ctrl+I, not Ctrl+N: "new" belongs to the open page (e.g. a new article).
    importAct = new QAction(AguaraStyle::icon(QStringLiteral("import")), tr("&Importar artículos..."), this);
    importAct->setObjectName(QStringLiteral("importAction"));
    importAct->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_I));
    importAct->setEnabled(false);               // until an import module loads
    connect(importAct, &QAction::triggered, this, [this]() { Import(); });

    backupAct = new QAction(AguaraStyle::icon(QStringLiteral("backup")), tr("&Copia de seguridad..."), this);
    backupAct->setObjectName(QStringLiteral("backupAction"));
    connect(backupAct, &QAction::triggered, this, &MainWindow::backupNow);

    quitAct = new QAction(AguaraStyle::icon(QStringLiteral("logout")), tr("&Salir"), this);
    quitAct->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_Q));
    connect(quitAct, &QAction::triggered, this, &QWidget::close);

    aboutAct = new QAction(AguaraStyle::icon(QStringLiteral("info")), tr("&Acerca de AguaraPOS"), this);
    connect(aboutAct, &QAction::triggered, this, &MainWindow::about);

    aboutModulesAct = new QAction(AguaraStyle::icon(QStringLiteral("puzzle")), tr("&Módulos instalados..."), this);
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
    helpMenu->addAction(aboutModulesAct);
    helpMenu->addSeparator();
    helpMenu->addAction(aboutAct);
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
        moduleButtons.append(nullptr);
        qCInfo(lcModules) << "Loaded module" << fileName << loaded.info.id << loaded.info.version;

        if (loaded.importer)
            addImportMenuEntry(modules.last());
        if (loaded.widgets)
            makeButton(modules.size() - 1);
    }
}

/// The module's theme icon in `color`, or its own icon.
QIcon MainWindow::moduleIcon(const LoadedModule &module, const QColor &color) const
{
    if (!module.info.iconName.isEmpty()) {
        const QIcon themed = AguaraStyle::icon(module.info.iconName, color);
        if (!themed.isNull())
            return themed;
    }
    return module.info.icon;
}

/// Add an "Importar desde" menu entry for a module that can import articles.
void MainWindow::addImportMenuEntry(const LoadedModule &module)
{
    importAct->setEnabled(true);

    // if this is the first import module, add the submenu
    if (importMenu == nullptr) {
        importMenu = new QMenu(tr("Importar &desde"), this);
        importMenu->setIcon(AguaraStyle::icon(QStringLiteral("import")));
        fileMenu->insertMenu(backupAct, importMenu);
    }

    const QString text = QString("%1 (%2)").arg(module.info.name,
                                                module.importer->fileTypes().join(", "));
    auto action = new QAction(moduleIcon(module, AguaraStyle::color(Color::Text)), text, this);
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

    QIcon icon = module.info.icon;
    if (!module.info.iconName.isEmpty())
        icon = sidebarIcon(module.info.iconName);
    QPushButton *button = makeNavButton(icon, module.info.name);
    moduleButtons[moduleIndex] = button;

    int position = 0;
    for (QPushButton *b : qAsConst(moduleButtons))
        position += b ? 1 : 0;
    button->setToolTip(position <= 9 ? tr("%1 (Ctrl+%2)").arg(module.info.name).arg(position)
                                     : module.info.name);
    noModulesLabel->hide();
    moduleNav->addWidget(button);

    connect(button, &QPushButton::clicked, this, [this, moduleIndex]() {
        openModulePage(moduleIndex);
        syncNavigation();
    });
}

/// The sidebar marks the module whose page is shown (or Avisos).
void MainWindow::syncNavigation()
{
    QWidget *current = ui->tabWidget->currentWidget();
    const int module = pageModule.value(current, -1);
    for (int i = 0; i < moduleButtons.size(); ++i) {
        if (moduleButtons.at(i))
            moduleButtons.at(i)->setChecked(i == module);
    }
    if (notificationsButton)
        notificationsButton->setChecked(current == &pageCero);
}

void MainWindow::dateTimeUpdate() {
    const QDateTime now = QDateTime::currentDateTime();
    // 24-hour "21:05": two-digit minutes, and no AM/PM text, which comes from
    // the system locale and is empty on some Spanish Windows installs
    timeLabel.setText(now.toString(QStringLiteral("HH:mm")));
    // "lunes 3 nov." in Spanish, whatever the Windows language
    dateLabel.setText(QLocale(QLocale::Spanish, QLocale::Paraguay).toString(now.date(), QStringLiteral("dddd d MMM")));
}

/// Show a module's page: re-focus the open one for single-instance modules,
/// otherwise create a new page. The tab widget owns the page from here on.
void MainWindow::openModulePage(int moduleIndex) {
    if (moduleIndex < 0 || moduleIndex >= modules.size() || !modules.at(moduleIndex).widgets)
        return;                     // not a module with a page
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
    pageModule.insert(page, moduleIndex);
    addTab(page, moduleIcon(module, AguaraStyle::color(Color::TextMuted)), module.info.name);
    page->setFocus();       // pages put the cursor where work starts (e.g. search)
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
    if (!page || page == &pageCero)      // the notifications page is permanent
        return;

    ui->tabWidget->removeTab(indx);
    for (LoadedModule &module : modules) {
        if (module.openPage == page)
            module.openPage = nullptr;  // don't hand out a page about to be deleted
    }
    pageModule.remove(page);
    page->deleteLater();
    syncNavigation();
}
