// The real MainWindow (no modules folder next to the test): notification
// cards, the status-bar clock, menus and shortcuts.
#include "corehost.h"
#include "mainwindow.h"
#include "notificationdialog.h"
#include "pagecero.h"

#include <QAction>
#include <QGridLayout>
#include <QLabel>
#include <QMenuBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QStatusBar>
#include <QTabWidget>
#include <QtTest>

class tst_MainWindow : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void sameSenderAndIdUpdatesTheCard();
    void priorityColor();
    void dismissDeletesAndRepacks();
    void lifetimeExpires();
    void clockIs24Hour();
    void fileMenuActionsHaveWorkingShortcuts();

private:
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
    NotificationDialog *updated = card(QStringLiteral("Inv - First (updated)"));
    QVERIFY(updated);
    QCOMPARE(updated->findChild<QLabel *>(QStringLiteral("message"))->text(), QStringLiteral("uno"));
    QVERIFY(!card(QStringLiteral("Inv - First")));
}

void tst_MainWindow::priorityColor()
{
    notify(ModuleMsg("Inv", "b", "Urgent", "two", MsgPriority::PriorityVeryHigh));
    NotificationDialog *c = card(QStringLiteral("Inv - Urgent"));
    QVERIFY(c);
    QVERIFY2(c->styleSheet().contains(QStringLiteral("rgb(255, 0, 0)")), qPrintable(c->styleSheet()));
}

void tst_MainWindow::dismissDeletesAndRepacks()
{
    notify(ModuleMsg("A", "1", "One", "x"));
    notify(ModuleMsg("A", "2", "Two", "x"));
    notify(ModuleMsg("A", "3", "Three", "x"));
    QPointer<NotificationDialog> two = card(QStringLiteral("A - Two"));
    QVERIFY(two);
    two->findChild<QPushButton *>(QStringLiteral("pushButton"))->click();
    pump(100);
    QVERIFY(two.isNull());
    QCOMPARE(cards().size(), m_initial + 2);

    // the remaining cards fill grid positions 0..n-1 without a hole
    auto grid = m_page->findChild<QGridLayout *>(QStringLiteral("gridLayoutMain"));
    QVERIFY(grid);
    QList<int> positions;
    for (NotificationDialog *c : cards()) {
        int r, col, rs, cs;
        grid->getItemPosition(grid->indexOf(c), &r, &col, &rs, &cs);
        positions << r * 3 + col;
    }
    std::sort(positions.begin(), positions.end());
    for (int i = 0; i < positions.size(); ++i)
        QCOMPARE(positions.at(i), i);
}

void tst_MainWindow::lifetimeExpires()
{
    notify(ModuleMsg("Sys", "tmp", "Short", "goes away", MsgPriority::PriorityLow, 0.3));
    QCOMPARE(cards().size(), m_initial + 1);
    pump(800);
    QCOMPARE(cards().size(), m_initial);
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

QTEST_MAIN(tst_MainWindow)
#include "tst_mainwindow.moc"
