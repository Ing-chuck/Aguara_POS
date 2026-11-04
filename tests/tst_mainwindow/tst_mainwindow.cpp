// The real MainWindow (no modules folder next to the test): notification
// cards, the status-bar clock, menus and shortcuts.
#include "corehost.h"
#include "mainwindow.h"
#include "passwordhash.h"
#include "sqluserservice.h"
#include "testsupport.h"
#include "notificationdialog.h"
#include "pagecero.h"

#include <QAbstractButton>
#include <QAction>
#include <QDialog>
#include <QLineEdit>
#include <QTimer>
#include <QGridLayout>
#include <QLabel>
#include <QMenuBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QStatusBar>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QtTest>

class tst_MainWindow : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void sameSenderAndIdUpdatesTheCard();
    void priorityLevel_data();
    void priorityLevel();
    void newestFirstAndDismissCloses();
    void lifetimeExpires();
    void badgeCountsNotifications();
    void emptyStateAndDismissAll();
    void sidebarMarksTheCurrentPage();
    void clockIs24Hour();
    void fileMenuActionsHaveWorkingShortcuts();
    void cashierSeesOnlyWhatTheyMayOpen();
    void lockedSessionCanBeTakenOverByAnotherUser();

private:
    struct Session;
    QPushButton *navButton(MainWindow *window, const QString &text) const;
    QList<NotificationDialog *> cards() const { return m_page->findChildren<NotificationDialog *>(); }
    void notify(const ModuleMsg &m) { emit m_host->notificationRequested(m); }
    NotificationDialog *card(const QString &title) const;
    static void pump(int ms);

    CoreHost *m_host = nullptr;
    MainWindow *m_window = nullptr;
    PageCero *m_page = nullptr;
    int m_initial = 0;
};

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

void tst_MainWindow::badgeCountsNotifications()
{
    auto badge = m_window->findChild<QLabel *>(QStringLiteral("notificationsBadge"));
    QVERIFY(badge);
    notify(ModuleMsg("B", "1", "Uno", "x"));
    notify(ModuleMsg("B", "2", "Dos", "x"));
    QVERIFY(badge->isVisible());
    QCOMPARE(badge->text(), QString::number(m_initial + 2));
    card(QStringLiteral("Uno"))->close();
    pump(100);
    QCOMPARE(badge->text(), QString::number(m_initial + 1));
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
    QVERIFY(!m_window->findChild<QLabel *>(QStringLiteral("notificationsBadge"))->isVisible());
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

QTEST_MAIN(tst_MainWindow)
#include "tst_mainwindow.moc"
