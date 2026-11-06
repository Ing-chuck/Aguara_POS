// The real MainWindow (no modules folder next to the test): notification
// cards, the status-bar clock, menus and shortcuts.
#include "corehost.h"
#include "mainwindow.h"
#include "passwordhash.h"
#include "sqluserservice.h"
#include "testsupport.h"
#include "notificationdialog.h"
#include "lowstockwatcher.h"
#include "pagecero.h"
#include "sessiondialogs.h"

#include <QAbstractButton>
#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QLineEdit>
#include <QTimer>
#include <QGridLayout>
#include <QLabel>
#include <QMenuBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QtTest>

class tst_MainWindow : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void sameSenderAndIdUpdatesTheCard();
    void blankSignInIsNotAnAttempt();
    void priorityLevel_data();
    void priorityLevel();
    void newestFirstAndDismissCloses();
    void lifetimeExpires();
    void dotMarksWhatNobodySaw();
    void emptyStateAndDismissAll();
    void sidebarMarksTheCurrentPage();
    void clockIs24Hour();
    void fileMenuActionsHaveWorkingShortcuts();
    void cashierSeesOnlyWhatTheyMayOpen();
    void lockedSessionCanBeTakenOverByAnotherUser();
    void openRegisterKeepsSalesOpen();
    void closedRegisterOpensNothing();
    void saleSurvivesAChangeOfUser();
    void userWhoCantSellDoesNotInheritThePage();
    void connectionComesFromTheMenu();
    void connectionNeedsPermission();
    void aConnectionThatFailsIsNotSaved();
    void printerAndStoreFromTheMenu();
    void cashierCantLeaveWithTheTillOpen();
    void closedTillLeavesFreely();
    void whoMayLeaveLeaves();
    void lockScreenCantLeaveWithTheTillOpen();
    void lockScreenLeavesWithTheTillClosed();
    void restartAfterAConnectionChangeIsNotStopped();
    void modulesThatCantRunAreReported();
    void installedModulesAreOnlyInTheHelpMenu();
    void notificationsCloseWhileTheTillIsOpen();
    void closingEverythingFallsBackOnNotifications();
    void lowStockReachesWhoMayRestock();

private:
    struct Session;
    QPushButton *navButton(MainWindow *window, const QString &text) const;
    QList<NotificationDialog *> cards() const { return m_page->findChildren<NotificationDialog *>(); }
    void notify(const ModuleMsg &m) { emit m_host->notificationRequested(m); }
    NotificationDialog *card(const QString &title) const;
    static void pump(int ms);
    static void activate(QWidget *window);

    CoreHost *m_host = nullptr;
    MainWindow *m_window = nullptr;
    PageCero *m_page = nullptr;
    int m_initial = 0;
};

void tst_MainWindow::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);   // never touch the real aguara.ini
    QCoreApplication::setOrganizationName(QStringLiteral("AguaraTest"));
    QCoreApplication::setApplicationName(QStringLiteral("tst_mainwindow"));
    QFile::remove(DatabaseSettings::settingsPath());
}

void tst_MainWindow::init()
{
    m_host = new CoreHost;
    m_window = new MainWindow(m_host);
    m_window->show();
    QApplication::setActiveWindow(m_window);    // shortcuts only reach the active window
    QVERIFY(QTest::qWaitForWindowActive(m_window));
    m_page = m_window->findChild<PageCero *>();
    QVERIFY(m_page);
    m_initial = cards().size();
}

void tst_MainWindow::cleanup()
{
    delete m_window;
    delete m_host;
    m_window = nullptr;
    m_host = nullptr;
}

void tst_MainWindow::pump(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QTest::qWait(5);
    }
}

void tst_MainWindow::modulesThatCantRunAreReported()
{
    qunsetenv("AGUARA_OLD_MODULE_RAN");
    MainWindow window(m_host, nullptr, nullptr, QStringLiteral(AGUARA_TEST_FIXTURE_MODULES));
    QStringList messages;
    for (QLabel *label : window.findChild<PageCero *>()->findChildren<QLabel *>())
        messages << label->text();

    const auto mentions = [&messages](const QString &text) {
        return std::any_of(messages.cbegin(), messages.cend(),
                           [&text](const QString &m) { return m.contains(text); });
    };
    QVERIFY2(mentions(QStringLiteral("old_api.dll es para otra versión")), qPrintable(messages.join('|')));
    QVERIFY2(mentions(QStringLiteral("wont_load.dll no se pudo abrir")), qPrintable(messages.join('|')));
    // the metadata was enough: the old module's code never ran
    QVERIFY(!qEnvironmentVariableIsSet("AGUARA_OLD_MODULE_RAN"));
}

void tst_MainWindow::activate(QWidget *window)
{
    window->show();
    QApplication::setActiveWindow(window);
    QTRY_VERIFY(window->isActiveWindow());
}

NotificationDialog *tst_MainWindow::card(const QString &title) const
{
    for (NotificationDialog *c : cards()) {
        if (c->findChild<QLabel *>(QStringLiteral("title"))->text() == title)
            return c;
    }
    return nullptr;
}

void tst_MainWindow::sameSenderAndIdUpdatesTheCard()
{
    notify(ModuleMsg("Inv", "a", "First", "one"));
    notify(ModuleMsg("Inv", "b", "Second", "two"));
    notify(ModuleMsg("Imp", "a", "Third", "three"));        // same id, other sender: a new card
    QCOMPARE(cards().size(), m_initial + 3);

    notify(ModuleMsg("Inv", "a", "First (updated)", "uno"));
    QCOMPARE(cards().size(), m_initial + 3);
    NotificationDialog *updated = card(QStringLiteral("First (updated)"));
    QVERIFY(updated);
    QCOMPARE(updated->findChild<QLabel *>(QStringLiteral("message"))->text(), QStringLiteral("uno"));
    QVERIFY(updated->findChild<QLabel *>(QStringLiteral("meta"))->text().startsWith(QStringLiteral("Inv · ")));
    QVERIFY(!card(QStringLiteral("First")));
}

void tst_MainWindow::priorityLevel_data()
{
    QTest::addColumn<int>("priority");
    QTest::addColumn<QString>("level");
    QTest::newRow("very high") << int(PriorityVeryHigh) << "danger";
    QTest::newRow("high")      << int(PriorityHigh)     << "warning";
    QTest::newRow("normal")    << int(PriorityNormal)   << "info";
    QTest::newRow("low")       << int(PriorityLow)      << "success";
    QTest::newRow("very low")  << int(PriorityVeryLow)  << "neutral";
}

void tst_MainWindow::priorityLevel()
{
    QFETCH(int, priority);
    QFETCH(QString, level);
    notify(ModuleMsg("Inv", "p", "Prioridad", "x", MsgPriority(priority)));
    NotificationDialog *c = card(QStringLiteral("Prioridad"));
    QVERIFY(c);
    QCOMPARE(c->property("level").toString(), level);       // drives the stripe and icon color
    QVERIFY(!c->findChild<QLabel *>(QStringLiteral("levelIcon"))->pixmap(Qt::ReturnByValue).isNull());
}

void tst_MainWindow::newestFirstAndDismissCloses()
{
    notify(ModuleMsg("A", "1", "One", "x"));
    notify(ModuleMsg("A", "2", "Two", "x"));
    notify(ModuleMsg("A", "3", "Three", "x"));
    auto layout = m_page->findChild<QVBoxLayout *>(QStringLiteral("cardsLayout"));
    QVERIFY(layout);
    QCOMPARE(layout->indexOf(card(QStringLiteral("Three"))), 0);     // newest on top
    QCOMPARE(layout->indexOf(card(QStringLiteral("One"))), 2);

    QPointer<NotificationDialog> two = card(QStringLiteral("Two"));
    QVERIFY(two);
    two->findChild<QAbstractButton *>(QStringLiteral("dismissButton"))->click();
    pump(100);
    QVERIFY(two.isNull());
    QCOMPARE(cards().size(), m_initial + 2);
    QCOMPARE(layout->indexOf(card(QStringLiteral("Three"))), 0);     // no gap left behind
    QCOMPARE(layout->indexOf(card(QStringLiteral("One"))), 1);
}

void tst_MainWindow::lifetimeExpires()
{
    notify(ModuleMsg("Sys", "tmp", "Short", "goes away", MsgPriority::PriorityLow, 0.3));
    QCOMPARE(cards().size(), m_initial + 1);
    pump(800);
    QCOMPARE(cards().size(), m_initial);
}

void tst_MainWindow::emptyStateAndDismissAll()
{
    auto empty = m_page->findChild<QWidget *>(QStringLiteral("emptyState"));
    auto dismissAll = m_page->findChild<QPushButton *>(QStringLiteral("dismissAllButton"));
    QVERIFY(empty && dismissAll);
    notify(ModuleMsg("C", "1", "Uno", "x"));
    notify(ModuleMsg("C", "2", "Dos", "x"));
    QVERIFY(!empty->isVisible());
    dismissAll->click();
    pump(100);
    QCOMPARE(cards().size(), 0);
    QVERIFY(empty->isVisible());
    QVERIFY(!dismissAll->isEnabled());
    QVERIFY(!m_window->findChild<QPushButton *>(QStringLiteral("notificationsButton"))
                 ->property("newNotifications").toBool());
}

void tst_MainWindow::sidebarMarksTheCurrentPage()
{
    auto avisos = m_window->findChild<QPushButton *>(QStringLiteral("notificationsButton"));
    QVERIFY(avisos);
    QVERIFY(avisos->isChecked());           // the notifications page is the start page
}

void tst_MainWindow::clockIs24Hour()
{
    // two-digit hours and minutes, no AM/PM text ("09:05", "21:05")
    const QRegularExpression clock(QStringLiteral("^([01]\\d|2[0-3]):[0-5]\\d$"));
    bool found = false;
    for (QLabel *l : m_window->statusBar()->findChildren<QLabel *>())
        found = found || clock.match(l->text()).hasMatch();
    QVERIFY(found);
}

void tst_MainWindow::fileMenuActionsHaveWorkingShortcuts()
{
    auto import = m_window->findChild<QAction *>(QStringLiteral("importAction"));
    QVERIFY(import);
    QCOMPARE(import->shortcut(), QKeySequence(Qt::CTRL + Qt::Key_I));
    QVERIFY(!import->associatedWidgets().isEmpty());          // in a menu: the shortcut is live
    QVERIFY(!import->isEnabled());                            // no import module loaded here

    auto backup = m_window->findChild<QAction *>(QStringLiteral("backupAction"));
    QVERIFY(backup);
    QVERIFY(!backup->associatedWidgets().isEmpty());

    // Ctrl+W never closes the notifications page
    auto tabs = m_window->findChild<QTabWidget *>();
    QVERIFY(tabs);
    const int before = tabs->count();
    QTest::keyClick(m_window, Qt::Key_W, Qt::ControlModifier);
    QCOMPARE(tabs->count(), before);
}

/// A real database with an administrator and a cashier, and the real modules.
struct tst_MainWindow::Session
{
    Session()
    {
        PasswordHash::setDefaultIterations(200);
        QString error;
        ok = core.open(DatabaseSettings::SQLite, tmp.path(), &error);
        users.reset(new SqlUserService(core.monitor.get()));
        core.host->registerService(UserService_iid, users.get());
        ok = ok && users->createFirstAdmin(QStringLiteral("admin"), QStringLiteral("La Jefa"), QStringLiteral("secreto1"));
        window.reset(new MainWindow(core.host.get(), nullptr, nullptr, QStringLiteral(AGUARA_TEST_MODULES_DIR)));
        window->setUsers(users.get(), 0);       // registers the modules' permissions
        UserAccount cashier;
        cashier.username = QStringLiteral("maria");
        cashier.fullName = QStringLiteral("Maria Cajera");
        for (const UserRole &r : users->roles()) {
            if (r.name == QLatin1String("Cajero"))
                cashier.roleIds << r.id;
        }
        ok = ok && users->saveUser(cashier, QStringLiteral("clave123"));
        ok = ok && users->login(QStringLiteral("maria"), QStringLiteral("clave123"));
        ok = ok && users->changePassword(QStringLiteral("clave123"), QStringLiteral("propia123"));
    }
    ~Session()
    {
        window.reset();
        users.reset();
        core.close();
    }

    QTemporaryDir tmp;
    TestSupport::CoreFixture core{ QStringLiteral("session") };
    std::unique_ptr<SqlUserService> users;
    std::unique_ptr<MainWindow> window;
    bool ok = false;
};

QPushButton *tst_MainWindow::navButton(MainWindow *window, const QString &text) const
{
    for (QPushButton *b : window->findChildren<QPushButton *>()) {
        if (b->text() == text && b->property("role").toString() == QLatin1String("nav"))
            return b;
    }
    return nullptr;
}

void tst_MainWindow::cashierSeesOnlyWhatTheyMayOpen()
{
    Session s;
    QVERIFY(s.ok);
    s.window->setUsers(s.users.get(), 0);       // the session now belongs to the cashier
    s.window->show();

    QVERIFY(navButton(s.window.get(), QStringLiteral("Ventas")));
    QVERIFY(!navButton(s.window.get(), QStringLiteral("Ventas"))->isHidden());
    QVERIFY(navButton(s.window.get(), QStringLiteral("Inventario"))->isHidden());
    QVERIFY(navButton(s.window.get(), QStringLiteral("Usuarios"))->isHidden());

    // not through a shortcut either: the modules load in file-name order
    auto tabs = s.window->findChild<QTabWidget *>();
    const int before = tabs->count();
    QMetaObject::invokeMethod(s.window.get(), "openModulePage", Q_ARG(int, 0));     // Inventario
    QCOMPARE(tabs->count(), before);

    QCOMPARE(s.window->findChild<QLabel *>(QStringLiteral("userLabel"))->text(), QStringLiteral("Maria Cajera"));

    // the administrator sees everything
    QVERIFY(s.users->login(QStringLiteral("admin"), QStringLiteral("secreto1")));
    s.window->setUsers(s.users.get(), 0);
    QVERIFY(!navButton(s.window.get(), QStringLiteral("Inventario"))->isHidden());
    QVERIFY(!navButton(s.window.get(), QStringLiteral("Usuarios"))->isHidden());
}

void tst_MainWindow::blankSignInIsNotAnAttempt()
{
    Session s;
    QVERIFY(s.ok);
    s.users->logout();

    LoginDialog dialog(s.users.get(), LoginDialog::Start);
    auto username = dialog.findChild<QLineEdit *>(QStringLiteral("username"));
    auto error = dialog.findChild<QLabel *>(QStringLiteral("errorLabel"));
    dialog.accept();
    QVERIFY(error->text().contains(QStringLiteral("usuario")));

    username->setText(QStringLiteral("admin"));
    for (int i = 0; i < 8; ++i)         // past the failures that lock an account
        dialog.accept();
    QVERIFY(error->text().contains(QStringLiteral("contraseña")));
    QVERIFY(s.users->login(QStringLiteral("admin"), QStringLiteral("secreto1")));
}

void tst_MainWindow::lockedSessionCanBeTakenOverByAnotherUser()
{
    Session s;
    QVERIFY(s.ok);
    QVERIFY(s.users->login(QStringLiteral("admin"), QStringLiteral("secreto1")));
    s.window->setUsers(s.users.get(), 0);
    s.window->show();
    auto tabs = s.window->findChild<QTabWidget *>();
    const int base = tabs->count();
    navButton(s.window.get(), QStringLiteral("Inventario"))->click();
    QCOMPARE(tabs->count(), base + 1);

    // type into the lock dialog once it is up
    QTimer::singleShot(100, [&]() {
        auto dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        QVERIFY(dialog);
        QTest::keyClick(dialog, Qt::Key_Escape);        // Esc doesn't get past it
        QVERIFY(dialog->isVisible());
        dialog->findChild<QLineEdit *>(QStringLiteral("username"))->setText(QStringLiteral("maria"));
        dialog->findChild<QLineEdit *>(QStringLiteral("password"))->setText(QStringLiteral("propia123"));
        dialog->accept();
    });
    QMetaObject::invokeMethod(s.window.get(), "lockSession");

    QCOMPARE(s.users->currentUser()->username, QStringLiteral("maria"));
    QCOMPARE(tabs->count(), base);              // the administrator's page is gone
    QVERIFY(navButton(s.window.get(), QStringLiteral("Inventario"))->isHidden());
}

static CashCount oneNote()
{
    DenominationCount note;
    note.denomination = 100000;
    note.count = 1;
    return { note };
}

static const auto closeButtonSide = QTabBar::RightSide;

void tst_MainWindow::openRegisterKeepsSalesOpen()
{
    Session s;
    QVERIFY(s.ok);
    QVERIFY(s.core.cash->openSession(oneNote()));
    s.window->setUsers(s.users.get(), 0);       // signing in with the till open
    s.window->show();

    auto tabs = s.window->findChild<QTabWidget *>();
    QCOMPARE(tabs->count(), 2);
    QCOMPARE(tabs->tabText(0), QStringLiteral("Ventas"));       // first, and in front
    QCOMPARE(tabs->currentIndex(), 0);
    QWidget *sales = tabs->widget(0);
    QVERIFY(!tabs->tabBar()->tabButton(0, closeButtonSide));

    QMetaObject::invokeMethod(tabs, "tabCloseRequested", Q_ARG(int, 0));
    QTest::keyClick(s.window.get(), Qt::Key_W, Qt::ControlModifier);
    QTest::qWait(0);
    QCOMPARE(tabs->count(), 2);
    QCOMPARE(tabs->widget(0), sales);

    // other pages can still be used, and closed
    QVERIFY(s.users->login(QStringLiteral("admin"), QStringLiteral("secreto1")));
    s.window->setUsers(s.users.get(), 0);
    navButton(s.window.get(), QStringLiteral("Inventario"))->click();
    QCOMPARE(tabs->count(), 3);
    QCOMPARE(tabs->currentIndex(), 2);
    QVERIFY(tabs->tabBar()->tabButton(2, closeButtonSide));

    // closing the till gives the X back, and the page can go
    QCOMPARE(s.core.cash->closeSession(oneNote()), CloseResult::Closed);
    emit qobject_cast<AguaraWidget *>(sales)->keepOpenChanged();
    QVERIFY(tabs->tabBar()->tabButton(0, closeButtonSide));
    QMetaObject::invokeMethod(tabs, "tabCloseRequested", Q_ARG(int, 0));
    QCOMPARE(tabs->count(), 2);
}

void tst_MainWindow::dotMarksWhatNobodySaw()
{
    Session s;
    QVERIFY(s.ok);
    QVERIFY(s.core.cash->openSession(oneNote()));
    auto avisos = s.window->findChild<QPushButton *>(QStringLiteral("notificationsButton"));
    auto page = s.window->findChild<PageCero *>();
    auto tabs = s.window->findChild<QTabWidget *>();
    const auto hasDot = [avisos]() { return avisos->property("newNotifications").toBool(); };
    const auto post = [&s](const QString &id) {
        emit s.core.host->notificationRequested(ModuleMsg("T", id, "Aviso", "x"));
    };

    // from while the program starts: Avisos is in front and looks seen, but
    // signing in with the till open puts Ventas over it before anyone reads it
    activate(s.window.get());
    post(QStringLiteral("early"));
    QVERIFY(!hasDot());
    s.window->setUsers(s.users.get(), 0);
    QCOMPARE(tabs->tabText(tabs->currentIndex()), QStringLiteral("Ventas"));
    QVERIFY(hasDot());
    QCOMPARE(avisos->toolTip(), QStringLiteral("Avisos nuevos (F1)"));

    // the dot is on the icon, and it's yellow
    const QImage marked = avisos->icon().pixmap(20, 20).toImage();
    QCOMPARE(marked.pixelColor(16, 4), QColor(0xFF, 0xC4, 0x00));

    // seeing them clears the dot; the notification stays
    const int count = page->count();
    avisos->click();
    QVERIFY(!hasDot());
    QCOMPARE(avisos->toolTip(), QStringLiteral("Avisos (F1)"));
    QCOMPARE(page->count(), count);
    QVERIFY(avisos->icon().pixmap(20, 20).toImage().pixelColor(16, 4) != QColor(0xFF, 0xC4, 0x00));

    post(QStringLiteral("seen"));                // arrives in front of the reader
    QVERIFY(!hasDot());

    // somebody uses something else, and comes back to Avisos. Only offscreen:
    // on a desktop Windows decides who gets the focus, not this test
    if (QGuiApplication::platformName() == QLatin1String("offscreen")) {
        activate(m_window);
        post(QStringLiteral("away"));
        QVERIFY(hasDot());
        activate(s.window.get());
        QVERIFY(!hasDot());
    }

    tabs->setCurrentIndex(0);
    post(QStringLiteral("late"));
    QVERIFY(hasDot());
    post(QStringLiteral("late"));                // an update of the same one
    QVERIFY(hasDot());

    page->dismissAll();                          // nothing left to see
    pump(100);
    QVERIFY(!hasDot());
}

void tst_MainWindow::installedModulesAreOnlyInTheHelpMenu()
{
    Session s;
    QVERIFY(s.ok);
    s.window->setUsers(s.users.get(), 0);
    QVERIFY(!navButton(s.window.get(), QStringLiteral("Módulos instalados")));
    QVERIFY(navButton(s.window.get(), QStringLiteral("Ventas")));

    bool inTheMenu = false;
    for (QAction *action : s.window->findChildren<QAction *>())
        inTheMenu = inTheMenu || action->text().remove('&').startsWith(QStringLiteral("Módulos instalados"));
    QVERIFY(inTheMenu);
}

void tst_MainWindow::notificationsCloseWhileTheTillIsOpen()
{
    Session s;
    QVERIFY(s.ok);
    auto tabs = s.window->findChild<QTabWidget *>();
    auto page = s.window->findChild<PageCero *>();
    s.window->setUsers(s.users.get(), 0);
    activate(s.window.get());                   // F1 and Ctrl+W need it
    QCOMPARE(tabs->indexOf(page), 0);
    QVERIFY(!tabs->tabBar()->tabButton(0, QTabBar::RightSide));    // the till is closed: no X

    QVERIFY(s.core.cash->openSession(oneNote()));
    s.window->setUsers(s.users.get(), 0);
    QCOMPARE(tabs->count(), 2);
    QCOMPARE(tabs->tabText(0), QStringLiteral("Ventas"));
    QVERIFY(tabs->tabBar()->tabButton(1, QTabBar::RightSide));

    QMetaObject::invokeMethod(tabs, "tabCloseRequested", Q_ARG(int, 1));
    QCOMPARE(tabs->count(), 1);
    QCOMPARE(tabs->indexOf(page), -1);

    // closed, it still collects notifications
    const int count = page->count();
    emit s.core.host->notificationRequested(ModuleMsg("T", "1", "Aviso", "x"));
    QCOMPARE(page->count(), count + 1);

    // F1 brings it back as it was, and it can be closed again
    QTest::keyClick(s.window.get(), Qt::Key_F1);
    QCOMPARE(tabs->count(), 2);
    QCOMPARE(tabs->currentWidget(), page);
    QCOMPARE(tabs->tabText(1), QStringLiteral("Avisos"));
    QVERIFY(!tabs->tabIcon(1).isNull());
    QVERIFY(tabs->tabBar()->tabButton(1, QTabBar::RightSide));
    QTest::keyClick(s.window.get(), Qt::Key_W, Qt::ControlModifier);
    QCOMPARE(tabs->count(), 1);

    // so does the sidebar, and closing the till takes the X away again
    s.window->findChild<QPushButton *>(QStringLiteral("notificationsButton"))->click();
    QCOMPARE(tabs->count(), 2);
    QWidget *sales = tabs->widget(0);
    QCOMPARE(s.core.cash->closeSession(oneNote()), CloseResult::Closed);
    emit qobject_cast<AguaraWidget *>(sales)->keepOpenChanged();
    QVERIFY(!tabs->tabBar()->tabButton(1, QTabBar::RightSide));
    QMetaObject::invokeMethod(tabs, "tabCloseRequested", Q_ARG(int, 1));
    QCOMPARE(tabs->count(), 2);
}

void tst_MainWindow::closingEverythingFallsBackOnNotifications()
{
    Session s;
    QVERIFY(s.ok);
    QVERIFY(s.core.cash->openSession(oneNote()));
    s.window->setUsers(s.users.get(), 0);
    s.window->show();
    auto tabs = s.window->findChild<QTabWidget *>();
    auto page = s.window->findChild<PageCero *>();
    QMetaObject::invokeMethod(tabs, "tabCloseRequested", Q_ARG(int, 1));
    QCOMPARE(tabs->count(), 1);                 // only Ventas

    QWidget *sales = tabs->widget(0);
    QCOMPARE(s.core.cash->closeSession(oneNote()), CloseResult::Closed);
    emit qobject_cast<AguaraWidget *>(sales)->keepOpenChanged();
    QMetaObject::invokeMethod(tabs, "tabCloseRequested", Q_ARG(int, 0));
    QCOMPARE(tabs->count(), 1);
    QCOMPARE(tabs->currentWidget(), page);
    QCOMPARE(tabs->tabText(0), QStringLiteral("Avisos"));
    QVERIFY(!tabs->tabBar()->tabButton(0, QTabBar::RightSide));
}

void tst_MainWindow::lowStockReachesWhoMayRestock()
{
    Session s;
    QVERIFY(s.ok);
    Product article;
    article.code = QStringLiteral("A");
    article.description = QStringLiteral("YERBA");
    article.price = 10000;
    article.stock = AguaraQuantity::whole(10);
    article.stockMin = AguaraQuantity::whole(5);
    QVERIFY(s.core.products->saveProduct(article));
    article.code = QStringLiteral("B");
    article.description = QStringLiteral("AZUCAR");
    QVERIFY(s.core.products->saveProduct(article));

    LowStockWatcher watcher(s.core.products.get(), s.core.inventory.get(), s.users.get(), s.core.host.get());
    connect(s.window.get(), &MainWindow::sessionChanged, &watcher, &LowStockWatcher::check);
    auto page = s.window->findChild<PageCero *>();
    auto tabs = s.window->findChild<QTabWidget *>();
    auto avisos = s.window->findChild<QPushButton *>(QStringLiteral("notificationsButton"));
    const auto stockCard = [page]() -> NotificationDialog * {
        for (NotificationDialog *c : page->findChildren<NotificationDialog *>()) {
            if (c->property("aguaraKey").toString().endsWith(QStringLiteral("stock-bajo")))
                return c;
        }
        return nullptr;
    };
    const auto text = [&stockCard]() { return stockCard()->findChild<QLabel *>(QStringLiteral("title"))->text(); };
    const auto move = [&s](const char *code, int units) {
        return s.core.inventory->adjustStock(QString::fromLatin1(code), AguaraQuantity::whole(units),
                                             QStringLiteral("prueba"));
    };

    // the cashier isn't who restocks: sign in the same way the program does
    s.window->setUsers(s.users.get(), 0);
    QVERIFY(move("A", -6));
    pump(50);
    QVERIFY(!stockCard());

    // whoever may opens Inventario hears at once, even with the page elsewhere
    QVERIFY(s.users->login(QStringLiteral("admin"), QStringLiteral("secreto1")));
    s.window->setUsers(s.users.get(), 0);
    pump(50);
    QVERIFY(stockCard());
    QCOMPARE(text(), QStringLiteral("1 artículo con stock bajo"));

    navButton(s.window.get(), QStringLiteral("Inventario"))->click();
    QVERIFY(move("B", -6));
    pump(50);
    QCOMPARE(text(), QStringLiteral("2 artículos con stock bajo"));
    QVERIFY(avisos->property("newNotifications").toBool());

    // restocking one isn't news: same card, nothing new to look at
    avisos->click();
    QVERIFY(!avisos->property("newNotifications").toBool());
    navButton(s.window.get(), QStringLiteral("Inventario"))->click();
    QCOMPARE(tabs->tabText(tabs->currentIndex()), QStringLiteral("Inventario"));
    QVERIFY(move("A", 20));
    pump(50);
    QCOMPARE(text(), QStringLiteral("1 artículo con stock bajo"));
    QVERIFY(!avisos->property("newNotifications").toBool());

    // dismissed, it doesn't come back for less than a new article
    stockCard()->close();
    pump(50);
    QVERIFY(!stockCard());
    QVERIFY(move("B", 1));
    pump(50);
    QVERIFY(!stockCard());
    QVERIFY(move("A", -20));
    pump(50);
    QVERIFY(stockCard());
    QVERIFY(avisos->property("newNotifications").toBool());

    // restocked, it goes
    QVERIFY(move("A", 20));
    QVERIFY(move("B", 20));
    pump(50);
    QVERIFY(!stockCard());
    QVERIFY(!avisos->property("newNotifications").toBool());

    // the card doesn't stay behind for a cashier who takes over
    QVERIFY(move("A", -20));
    pump(50);
    QVERIFY(stockCard());
    QVERIFY(s.users->login(QStringLiteral("maria"), QStringLiteral("propia123")));
    s.window->setUsers(s.users.get(), 0);
    pump(50);
    QVERIFY(!stockCard());
}

void tst_MainWindow::closedRegisterOpensNothing()
{
    Session s;
    QVERIFY(s.ok);
    s.window->setUsers(s.users.get(), 0);
    auto tabs = s.window->findChild<QTabWidget *>();
    QCOMPARE(tabs->count(), 1);

    navButton(s.window.get(), QStringLiteral("Ventas"))->click();
    QCOMPARE(tabs->count(), 2);
    QVERIFY(tabs->tabBar()->tabButton(1, closeButtonSide));
    QMetaObject::invokeMethod(tabs, "tabCloseRequested", Q_ARG(int, 1));
    QCOMPARE(tabs->count(), 1);
}

void tst_MainWindow::saleSurvivesAChangeOfUser()
{
    Session s;
    QVERIFY(s.ok);
    Product p;
    p.code = QStringLiteral("7840001");
    p.description = QStringLiteral("ARTICULO");
    p.price = 11000;
    p.stock = AguaraQuantity::whole(10);
    QVERIFY(s.core.products->saveProduct(p));
    QVERIFY(s.core.cash->openSession(oneNote()));
    s.window->setUsers(s.users.get(), 0);
    s.window->show();

    auto tabs = s.window->findChild<QTabWidget *>();
    QWidget *sales = tabs->widget(0);
    auto entry = sales->findChild<QLineEdit *>(QStringLiteral("saleEntry"));
    QTest::keyClicks(entry, p.code);
    QTest::keyClick(entry, Qt::Key_Return);
    auto lines = sales->findChild<QTableWidget *>(QStringLiteral("saleLines"));
    QCOMPARE(lines->rowCount(), 1);

    QVERIFY(s.users->login(QStringLiteral("admin"), QStringLiteral("secreto1")));
    s.window->setUsers(s.users.get(), 0);       // what a change of user does

    QCOMPARE(tabs->widget(0), sales);
    QCOMPARE(lines->rowCount(), 1);
}

void tst_MainWindow::userWhoCantSellDoesNotInheritThePage()
{
    Session s;
    QVERIFY(s.ok);
    QVERIFY(s.core.cash->openSession(oneNote()));
    s.window->setUsers(s.users.get(), 0);
    auto tabs = s.window->findChild<QTabWidget *>();
    QCOMPARE(tabs->count(), 2);

    QVERIFY(s.users->login(QStringLiteral("admin"), QStringLiteral("secreto1")));
    UserRole none;
    none.name = QStringLiteral("Sin ventas");
    QVERIFY(s.users->saveRole(none));
    UserAccount other;
    other.username = QStringLiteral("otro");
    other.fullName = QStringLiteral("Otro");
    for (const UserRole &r : s.users->roles()) {
        if (r.name == none.name)
            other.roleIds << r.id;
    }
    QVERIFY(s.users->saveUser(other, QStringLiteral("clave123")));
    QVERIFY(s.users->login(QStringLiteral("otro"), QStringLiteral("clave123")));
    s.window->setUsers(s.users.get(), 0);

    QCOMPARE(tabs->count(), 1);                 // Avisos alone
    QTest::qWait(0);
}

/// `steps` runs on the dialog that is about to open.
static void whenDialogOpens(const std::function<void(QDialog *)> &steps)
{
    QTimer::singleShot(100, qApp, [steps]() {
        auto dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        QVERIFY(dialog);
        steps(dialog);
        if (dialog->isVisible())
            dialog->reject();
    });
}

void tst_MainWindow::connectionComesFromTheMenu()
{
    QFile::remove(DatabaseSettings::settingsPath());
    Session s;
    QVERIFY(s.ok);
    QVERIFY(s.users->login(QStringLiteral("admin"), QStringLiteral("secreto1")));
    s.window->setUsers(s.users.get(), 0);
    QSignalSpy restart(s.window.get(), SIGNAL(restartRequested()));
    const QString file = s.tmp.filePath(QStringLiteral("otra.db"));

    bool asked = false;
    whenDialogOpens([&](QDialog *settings) {
        QVERIFY(settings->findChild<QLabel *>(QStringLiteral("databaseNotice"))->text().contains(QStringLiteral("no se copian")));
        settings->findChild<QLineEdit *>(QStringLiteral("databaseFile"))->setText(file);
        whenDialogOpens([&](QDialog *ask) {
            asked = true;
            QVERIFY(ask->findChildren<QLabel *>().last()->text().contains(QStringLiteral("otra.db")));
            ask->reject();                      // "later": saved, nothing restarts
        });
        settings->accept();
    });
    s.window->findChild<QAction *>(QStringLiteral("connectionAction"))->trigger();
    QVERIFY(asked);
    QCOMPARE(QDir::cleanPath(DatabaseSettings::load().sqlitePath), QDir::cleanPath(file));
    QCOMPARE(restart.count(), 0);

    whenDialogOpens([&](QDialog *settings) {
        settings->findChild<QLineEdit *>(QStringLiteral("databaseFile"))->setText(s.tmp.filePath(QStringLiteral("tercera.db")));
        whenDialogOpens([](QDialog *ask) { ask->accept(); });
        settings->accept();
    });
    s.window->findChild<QAction *>(QStringLiteral("connectionAction"))->trigger();
    QCOMPARE(restart.count(), 1);

    // nothing changed: nothing to ask
    asked = false;
    whenDialogOpens([&](QDialog *settings) {
        QTimer::singleShot(100, qApp, [&]() {
            if (auto ask = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
                asked = true;
                ask->reject();
            }
        });
        settings->accept();
    });
    s.window->findChild<QAction *>(QStringLiteral("connectionAction"))->trigger();
    QTest::qWait(150);
    QVERIFY(!asked);
    QCOMPARE(restart.count(), 1);
    QFile::remove(DatabaseSettings::settingsPath());
}

void tst_MainWindow::connectionNeedsPermission()
{
    QFile::remove(DatabaseSettings::settingsPath());
    Session s;
    QVERIFY(s.ok);
    int asked = 0;
    s.users->setSupervisorPrompt([&](const QString &, const QString &, const QString &, QWidget *, QString *, QString *) {
        ++asked;
        return false;
    });
    s.window->findChild<QAction *>(QStringLiteral("connectionAction"))->trigger();      // the cashier
    QCOMPARE(asked, 1);
    QVERIFY(!QFile::exists(DatabaseSettings::settingsPath()));

    s.window->setConnectionEditable(false);     // --database: aguara.ini is not in use
    QVERIFY(!s.window->findChild<QAction *>(QStringLiteral("connectionAction"))->isEnabled());
}

void tst_MainWindow::aConnectionThatFailsIsNotSaved()
{
    QFile::remove(DatabaseSettings::settingsPath());
    Session s;
    QVERIFY(s.ok);
    QVERIFY(s.users->login(QStringLiteral("admin"), QStringLiteral("secreto1")));
    s.window->setUsers(s.users.get(), 0);

    whenDialogOpens([](QDialog *settings) {
        auto backend = settings->findChild<QComboBox *>(QStringLiteral("databaseBackend"));
        backend->setCurrentIndex(backend->findData(int(DatabaseSettings::PostgreSQL)));
        settings->findChild<QLineEdit *>(QStringLiteral("databaseHost"))->clear();      // refused without any network
        settings->accept();
        QVERIFY(settings->isVisible());
        QVERIFY(!settings->findChild<QLabel *>(QStringLiteral("databaseStatus"))->text().isEmpty());
    });
    s.window->findChild<QAction *>(QStringLiteral("connectionAction"))->trigger();
    QVERIFY(!QFile::exists(DatabaseSettings::settingsPath()));
}

void tst_MainWindow::printerAndStoreFromTheMenu()
{
    Session s;
    QVERIFY(s.ok);
    QVERIFY(s.users->login(QStringLiteral("admin"), QStringLiteral("secreto1")));
    s.window->setUsers(s.users.get(), 0);

    whenDialogOpens([](QDialog *dialog) {
        auto kind = dialog->findChild<QComboBox *>(QStringLiteral("printerKind"));
        kind->setCurrentIndex(kind->findData(int(PrinterKind::Thermal)));
        auto name = dialog->findChild<QComboBox *>(QStringLiteral("printerName"));
        name->clear();
        name->addItem(QStringLiteral("TERMICA"));
        name->setCurrentIndex(0);
        dialog->findChild<QPushButton *>(QStringLiteral("printerTest"))->click();
        QVERIFY(dialog->findChild<QLabel *>(QStringLiteral("printerStatus"))->text().contains(QStringLiteral("TERMICA")));
        dialog->findChild<QLineEdit *>(QStringLiteral("storeName"))->setText(QStringLiteral("ALMACEN"));
        dialog->accept();
    });
    s.window->findChild<QAction *>(QStringLiteral("printerAction"))->trigger();

    QCOMPARE(s.core.printed.size(), 1);         // the test print, on the settings not saved yet
    QCOMPARE(s.core.print->printerSettings().name, QStringLiteral("TERMICA"));
    QCOMPARE(s.core.print->printerSettings().kind, PrinterKind::Thermal);
    QCOMPARE(s.core.print->storeInfo().name, QStringLiteral("ALMACEN"));
}

/// "Salir del programa" on the lock screen.
static QAbstractButton *exitButton(QDialog *dialog)
{
    auto box = dialog->findChild<QDialogButtonBox *>();
    for (QAbstractButton *button : box->buttons()) {
        if (box->buttonRole(button) == QDialogButtonBox::RejectRole)
            return button;
    }
    return nullptr;
}

void tst_MainWindow::cashierCantLeaveWithTheTillOpen()
{
    Session s;
    QVERIFY(s.ok);
    QVERIFY(s.core.cash->openSession(oneNote()));
    s.window->setUsers(s.users.get(), 0);
    s.window->show();

    int asked = 0;
    bool authorize = false;
    QString shown;
    s.users->setSupervisorPrompt([&](const QString &, const QString &reason, const QString &, QWidget *,
                                     QString *username, QString *password) {
        ++asked;
        shown = reason;
        *username = QStringLiteral("admin");
        *password = QStringLiteral("secreto1");
        return authorize;
    });

    QVERIFY(!s.window->close());                // refused, or cancelled
    QVERIFY(s.window->isVisible());
    QCOMPARE(asked, 1);
    QVERIFY(shown.contains(QStringLiteral("abierta")));

    authorize = true;
    QVERIFY(s.window->close());
    QCOMPARE(asked, 2);
    bool audited = false;
    for (const AuditEntry &e : s.users->auditEntries())
        audited = audited || e.action == QLatin1String("caja.salir_abierta");
    QVERIFY(audited);
}

void tst_MainWindow::closedTillLeavesFreely()
{
    Session s;
    QVERIFY(s.ok);
    QVERIFY(s.core.cash->openSession(oneNote()));
    s.window->setUsers(s.users.get(), 0);
    s.window->show();
    QCOMPARE(s.core.cash->closeSession(oneNote()), CloseResult::Closed);

    int asked = 0;
    s.users->setSupervisorPrompt([&](const QString &, const QString &, const QString &, QWidget *, QString *, QString *) {
        ++asked;
        return false;
    });
    QVERIFY(s.window->close());                 // the cashier closed the till: free to go
    QCOMPARE(asked, 0);
}

void tst_MainWindow::whoMayLeaveLeaves()
{
    Session s;
    QVERIFY(s.ok);
    QVERIFY(s.core.cash->openSession(oneNote()));
    QVERIFY(s.users->login(QStringLiteral("admin"), QStringLiteral("secreto1")));
    s.window->setUsers(s.users.get(), 0);
    s.window->show();
    QVERIFY(s.window->close());
}

/// Who locked is still signed in: an administrator's permission must not
/// open the door to whoever finds the lock screen.
void tst_MainWindow::lockScreenCantLeaveWithTheTillOpen()
{
    Session s;
    QVERIFY(s.ok);
    QVERIFY(s.core.cash->openSession(oneNote()));
    QVERIFY(s.users->login(QStringLiteral("admin"), QStringLiteral("secreto1")));
    s.window->setUsers(s.users.get(), 0);
    s.window->show();

    bool checked = false;
    QTimer::singleShot(100, [&]() {
        auto dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        QVERIFY(dialog);
        QVERIFY(exitButton(dialog)->isHidden());
        dialog->reject();
        QVERIFY(dialog->isVisible());
        checked = true;
        dialog->findChild<QLineEdit *>(QStringLiteral("username"))->setText(QStringLiteral("admin"));
        dialog->findChild<QLineEdit *>(QStringLiteral("password"))->setText(QStringLiteral("secreto1"));
        dialog->accept();
    });
    QMetaObject::invokeMethod(s.window.get(), "lockSession");
    QVERIFY(checked);
    QVERIFY(s.window->isVisible());
}

void tst_MainWindow::lockScreenLeavesWithTheTillClosed()
{
    Session s;
    QVERIFY(s.ok);
    s.window->setUsers(s.users.get(), 0);
    s.window->show();
    QTimer::singleShot(100, [&]() {
        auto dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        QVERIFY(dialog);
        exitButton(dialog)->click();
    });
    QMetaObject::invokeMethod(s.window.get(), "lockSession");
    QVERIFY(!s.window->isVisible());
}

void tst_MainWindow::restartAfterAConnectionChangeIsNotStopped()
{
    Session s;
    QVERIFY(s.ok);
    QVERIFY(s.core.cash->openSession(oneNote()));
    s.window->setUsers(s.users.get(), 0);
    s.window->show();
    s.window->closeForRestart();
    QVERIFY(!s.window->isVisible());
}

QTEST_MAIN(tst_MainWindow)
#include "tst_mainwindow.moc"
