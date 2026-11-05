// The real module plugins (built into <build>/bin/modules), driven through
// the same host and services the application gives them, on SQLite.
#include "testsupport.h"

#include "closingsheet.h"
#include "labelsheet.h"
#include "passwordhash.h"
#include "sqluserservice.h"

#include <interfaces.h>

#include <QDialog>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QListWidget>
#include <QPluginLoader>
#include <QAbstractButton>
#include <QAction>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QSpinBox>
#include <QFileInfo>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPrinter>
#include <QPushButton>
#include <QTableView>
#include <QTimer>
#include <QTableWidget>
#include <QTabWidget>
#include <QtTest>

using TestSupport::CoreFixture;
using TestSupport::inCash;
using TestSupport::byVoucher;

class tst_Modules : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // modulo_importar
    void importMergesWithExistingArticles();
    void importQuotedFields();
    void importExcelWindows1252();

    // modulo_control_inventario: keyboard and barcode scanner
    void inventoryScannerSelectsCode();
    void inventorySearchByDescription();
    void inventoryNotFoundNotifies();
    void inventoryAsksForAuthorization();
    void usersPageShowsWhatTheRoleAllows();
    void purchaseThroughTheScreen();
    void scannerNeverSavesThePurchase();
    void countThroughTheScreen();
    void adjustThroughTheScreen();
    void labelsFillPages();

    // modulo_sales_module: a day at the till, through the screen
    void aDayAtTheTill();
    void scanningProblemsStayOnThePage();
    void quantityPrefixAndRepeatedScans();
    void scaleLabelsAtTheTill();
    void wrongCountIsBlindAndNeedsASupervisor();
    void discountOverTheLimitNeedsASupervisor();
    void voidingFromTheScreen();
    void cardPaysEverything();
    void cardAndCashTogether();
    void cancellingWarnsAboutVouchers();
    void repeatedVoucherNeedsASupervisor();
    void voidingACardSaysToReverseIt();
    void outageOnlyFinishesTheSaleInProgress();
    void customerOfTheSale();
    void returnThroughTheScreen();
    void returnNeedsPermission();
    void creditNoteThroughThePaymentScreen();
    void paymentKeysFollowTheMethods();
    void configuringPaymentMethods();
    void customersPage();
    void reportsPage();
    void chargingPrintsAndOpensTheDrawer();
    void printerFailureDoesNotStopTheSale();
    void copyNeedsASupervisor();
    void drawerByHandNeedsPermission();
    void closingSheetGoesToTheThermalPrinter();
    void holdAndRecall();
    void forgottenSessionNeedsASupervisor();
    void searchByDescription();
    void closingSheetShowsTheFigures();

private:
    void openUsers();
    bool showSales();
    bool openTill(Money hundredThousands = 1);
    void scan(const QString &text);
    void press(Qt::Key key);
    QLabel *label(const char *name) const { return m_page->findChild<QLabel *>(QLatin1String(name)); }
    QTableWidget *saleLines() const { return m_page->findChild<QTableWidget *>(QStringLiteral("saleLines")); }
    QLineEdit *saleEntry() const { return m_page->findChild<QLineEdit *>(QStringLiteral("saleEntry")); }
    void trigger(const char *action) { m_page->findChild<QAction *>(QLatin1String(action))->trigger(); }
    QObject *load(const QString &fileName);
    QString writeFile(const QString &name, const QByteArray &bytes);
    QTableView *showInventory(QLineEdit **search);

    QTemporaryDir m_tmp;
    CoreFixture m_core;
    std::unique_ptr<SqlUserService> m_users;
    QList<QPluginLoader *> m_loaders;
    QWidget *m_page = nullptr;
    QString m_lastNote;
};

void tst_Modules::init()
{
    QString error;
    QVERIFY2(m_core.open(DatabaseSettings::SQLite, m_tmp.path(), &error), qPrintable(error));
    m_lastNote.clear();
    connect(m_core.host.get(), &CoreHost::notificationRequested, this,
            [this](const ModuleMsg &m) { m_lastNote = m.title + QStringLiteral(": ") + m.message; });
}

void tst_Modules::cleanup()
{
    QTest::qWait(0);
    delete m_page;
    m_page = nullptr;
    m_users.reset();
    m_core.close();
    // plugins stay loaded (Qt shares one instance per file); just forget the loaders
    qDeleteAll(m_loaders);
    m_loaders.clear();
}

QObject *tst_Modules::load(const QString &fileName)
{
    auto loader = new QPluginLoader(QDir(QStringLiteral(AGUARA_TEST_MODULES_DIR)).filePath(fileName));
    m_loaders << loader;
    QObject *plugin = loader->instance();
    if (!plugin)
        qWarning() << "cannot load" << fileName << loader->errorString();
    auto module = qobject_cast<AguaraModuleInterface *>(plugin);
    if (!module || !module->initialize(m_core.host.get()))
        return nullptr;
    return plugin;
}

QString tst_Modules::writeFile(const QString &name, const QByteArray &bytes)
{
    const QString path = m_tmp.filePath(name);
    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write(bytes);
    return path;
}

void tst_Modules::importMergesWithExistingArticles()
{
    auto importer = qobject_cast<ImportArticlesInterface *>(load(QStringLiteral("modulo_importar.dll")));
    QVERIFY(importer);
    QVERIFY(importer->fileTypes().contains(QStringLiteral("csv")));

    Product existing;
    existing.code = QStringLiteral("777"); existing.description = QStringLiteral("GASEOSA 2.25L");
    existing.price = 15000; existing.cost = 11000; existing.stock = AguaraQuantity::whole(24);
    QVERIFY(m_core.products->saveProduct(existing));

    const QString csv = writeFile(QStringLiteral("precios.csv"), QString::fromUtf8(
        "Código;Descripción;Marca;Precio Venta;IVA\n"
        "777;GASEOSA 2.25L;COCA COLA;16.500;10\n"
        "2002;YERBA 500G;KURUPI;12.000;5\n"
        "3003;SIN PRECIO;;abc;10\n").toUtf8());
    QVERIFY(importer->importFrom(csv));
    QVERIFY2(m_lastNote.contains(QString::fromUtf8("2 artículos importados")), qPrintable(m_lastNote));
    QVERIFY2(m_lastNote.contains(QString::fromUtf8("línea 4")), qPrintable(m_lastNote));     // the bad row is reported

    const auto updated = m_core.products->product(QStringLiteral("777"));
    QCOMPARE(updated->price, Money(16500));
    QCOMPARE(updated->stock, AguaraQuantity::whole(24));    // not in the file: kept
    QCOMPARE(updated->cost, Money(11000));
    QCOMPARE(updated->brandName, QStringLiteral("COCA COLA"));
    const auto added = m_core.products->product(QStringLiteral("2002"));
    QVERIFY(added.has_value());
    QCOMPARE(added->vatRate, 5);
    QCOMPARE(added->brandName, QStringLiteral("KURUPI"));
    QVERIFY(!m_core.products->product(QStringLiteral("3003")).has_value());
}

void tst_Modules::importQuotedFields()
{
    auto importer = qobject_cast<ImportArticlesInterface *>(load(QStringLiteral("modulo_importar.dll")));
    QVERIFY(importer);
    const QString csv = writeFile(QStringLiteral("quoted.csv"),
        "Codigo,Descripcion,Marca,Precio\r\n"
        "101,\"GALLETITAS, SURTIDAS\",\"ARCOR\",\"12,500\"\r\n"
        "102,\"VINO \"\"TINTO\"\" 750ML\",,25000\r\n"
        "103,\"CAJA\nGRANDE\",,1000\r\n");
    QVERIFY(importer->importFrom(csv));
    QCOMPARE(m_core.products->product(QStringLiteral("101"))->description, QStringLiteral("GALLETITAS, SURTIDAS"));
    QCOMPARE(m_core.products->product(QStringLiteral("101"))->price, Money(12500));
    QCOMPARE(m_core.products->product(QStringLiteral("101"))->brandName, QStringLiteral("ARCOR"));
    QCOMPARE(m_core.products->product(QStringLiteral("102"))->description, QStringLiteral("VINO \"TINTO\" 750ML"));
    QCOMPARE(m_core.products->product(QStringLiteral("103"))->description, QStringLiteral("CAJA\nGRANDE"));
}

void tst_Modules::importExcelWindows1252()
{
    auto importer = qobject_cast<ImportArticlesInterface *>(load(QStringLiteral("modulo_importar.dll")));
    QVERIFY(importer);
    // what Excel writes on Spanish Windows ("CSV (delimitado por comas)")
    const QString csv = writeFile(QStringLiteral("excel.csv"),
        "C\xF3" "digo;Descripci\xF3" "n;Precio Venta\r\n201;CA\xD1" "A DE AZ\xDA" "CAR;8.000\r\n");
    QVERIFY(importer->importFrom(csv));
    const auto p = m_core.products->product(QStringLiteral("201"));
    QVERIFY(p.has_value());
    QCOMPARE(p->description, QString::fromUtf8("CAÑA DE AZÚCAR"));
    QCOMPARE(p->price, Money(8000));
}

void tst_Modules::openUsers()
{
    PasswordHash::setDefaultIterations(200);
    m_users.reset(new SqlUserService(m_core.monitor.get()));
    m_core.host->registerService(UserService_iid, m_users.get());
    m_users->createFirstAdmin(QStringLiteral("admin"), QStringLiteral("Admin"), QStringLiteral("secreto1"));
    auto provider = qobject_cast<PermissionProvider *>(load(QStringLiteral("modulo_control_inventario.dll")));
    QVERIFY(provider);
    QList<PermissionInfo> permissions = provider->permissions();
    permissions += qobject_cast<PermissionProvider *>(load(QStringLiteral("modulo_usuarios.dll")))->permissions();
    QVERIFY(m_users->registerPermissions(permissions));

    UserAccount cashier, manager;
    cashier.username = QStringLiteral("cajero");
    cashier.fullName = QStringLiteral("Cajero");
    manager.username = QStringLiteral("encargado");
    manager.fullName = QStringLiteral("Encargado");
    for (const UserRole &r : m_users->roles()) {
        if (r.name == QLatin1String("Cajero"))
            cashier.roleIds << r.id;
        if (r.name == QLatin1String("Encargado"))
            manager.roleIds << r.id;
    }
    QVERIFY(m_users->saveUser(cashier, QStringLiteral("clave123")));
    QVERIFY(m_users->saveUser(manager, QStringLiteral("clave123")));
}

void tst_Modules::inventoryAsksForAuthorization()
{
    openUsers();
    QVERIFY(m_users->login(QStringLiteral("cajero"), QStringLiteral("clave123")));
    int asked = 0;
    m_users->setSupervisorPrompt([&](const QString &, const QString &, const QString &, QWidget *,
                                     QString *, QString *) {
        ++asked;
        return false;                           // the supervisor does not come
    });

    QLineEdit *search = nullptr;
    QTableView *table = showInventory(&search);
    QVERIFY(table);
    m_page->findChild<QPushButton *>(QStringLiteral("itemAddButton"))->click();
    QCOMPARE(asked, 1);
    table->selectRow(0);
    m_page->findChild<QPushButton *>(QStringLiteral("itemEditButton"))->click();
    QCOMPARE(asked, 2);
    for (QDialog *d : m_page->findChildren<QDialog *>())
        QVERIFY(!d->isVisible());               // no form opened
}

void tst_Modules::usersPageShowsWhatTheRoleAllows()
{
    openUsers();
    auto widgets = qobject_cast<WidgetModuleInterface *>(load(QStringLiteral("modulo_usuarios.dll")));
    QVERIFY(widgets);

    std::unique_ptr<QWidget> page(widgets->createWidget());     // administrator
    QCOMPARE(page->findChild<QTabWidget *>(QStringLiteral("usersTabs"))->count(), 3);
    QCOMPARE(page->findChild<QTableWidget *>(QStringLiteral("usersTable"))->rowCount(), 3);

    QVERIFY(m_users->login(QStringLiteral("encargado"), QStringLiteral("clave123")));
    page.reset(widgets->createWidget());
    QCOMPARE(page->findChild<QTabWidget *>(QStringLiteral("usersTabs"))->count(), 1);     // the audit log only
    QVERIFY(!page->findChild<QTableWidget *>(QStringLiteral("usersTable")));
    QVERIFY(page->findChild<QTableWidget *>(QStringLiteral("auditTable"))->rowCount() > 0);
}

/// A modal dialog is about to open: run `steps` on it once it's up.
static void whenDialogOpens(const std::function<void(QDialog *)> &steps)
{
    QTimer::singleShot(150, qApp, [steps]() {
        auto dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        QVERIFY(dialog);
        steps(dialog);
        if (dialog->isVisible())
            dialog->reject();       // a failed step must not leave the test waiting on exec()
    });
}

void tst_Modules::purchaseThroughTheScreen()
{
    QLineEdit *search = nullptr;
    QVERIFY(showInventory(&search));
    Supplier supplier;
    supplier.name = QStringLiteral("DISTRIBUIDORA");
    QVERIFY(m_core.inventory->saveSupplier(supplier));

    auto sections = m_page->findChild<QTabWidget *>(QStringLiteral("inventorySections"));
    QVERIFY(sections);
    QCOMPARE(sections->count(), 4);
    sections->setCurrentIndex(1);

    bool done = false;
    whenDialogOpens([&](QDialog *dialog) {
        dialog->findChild<QLineEdit *>(QStringLiteral("purchaseInvoice"))->setText(QStringLiteral("001-001-55"));
        auto entry = dialog->findChild<QLineEdit *>(QStringLiteral("purchaseEntry"));
        QTest::keyClicks(entry, QStringLiteral("7840002"));
        QTest::keyClick(entry, Qt::Key_Return);
        auto lines = dialog->findChild<QTableWidget *>(QStringLiteral("purchaseLines"));
        QCOMPARE(lines->rowCount(), 1);
        // the way a person does it: type in the cell, Tab to the next one
        const auto typeInCell = [lines](const QString &text, Qt::Key next) {
            auto editor = qobject_cast<QLineEdit *>(lines->indexWidget(lines->currentIndex()));
            QVERIFY(editor);
            editor->selectAll();
            QTest::keyClicks(editor, text);
            if (next != Qt::Key_unknown)
                QTest::keyClick(editor, next);
        };
        typeInCell(QStringLiteral("12"), Qt::Key_Tab);          // Cantidad
        typeInCell(QStringLiteral("4.000"), Qt::Key_Tab);       // Costo
        typeInCell(QStringLiteral("6.500"), Qt::Key_unknown);   // Precio nuevo, still being typed when saving
        QCOMPARE(lines->item(0, 5)->text(), QStringLiteral("48.000"));
        QVERIFY(dialog->findChild<QLabel *>(QStringLiteral("purchaseTotal"))->text().contains(QStringLiteral("48.000")));
        done = true;
        dialog->accept();
    });
    m_page->findChild<QPushButton *>(QStringLiteral("newPurchaseButton"))->click();
    QVERIFY(done);

    const auto p = m_core.products->product(QStringLiteral("7840002"));
    QCOMPARE(p->stock, AguaraQuantity::whole(12));
    QCOMPARE(p->cost, Money(4000));
    QCOMPARE(p->price, Money(6500));
    QCOMPARE(m_page->findChild<QTableWidget *>(QStringLiteral("purchasesTable"))->rowCount(), 1);
}

void tst_Modules::scannerNeverSavesThePurchase()
{
    QLineEdit *search = nullptr;
    QVERIFY(showInventory(&search));
    Supplier supplier;
    supplier.name = QStringLiteral("DISTRIBUIDORA");
    QVERIFY(m_core.inventory->saveSupplier(supplier));
    m_page->findChild<QTabWidget *>(QStringLiteral("inventorySections"))->setCurrentIndex(1);

    bool done = false;
    whenDialogOpens([&](QDialog *dialog) {
        dialog->findChild<QLineEdit *>(QStringLiteral("purchaseInvoice"))->setText(QStringLiteral("001-001-56"));
        auto entry = dialog->findChild<QLineEdit *>(QStringLiteral("purchaseEntry"));
        auto lines = dialog->findChild<QTableWidget *>(QStringLiteral("purchaseLines"));
        const auto scan = [entry]() {
            QTest::keyClicks(entry, QStringLiteral("7840002"));
            QTest::keyClick(entry, Qt::Key_Return);             // what the scanner sends after each code
        };
        scan();
        QVERIFY(dialog->isVisible());
        QCOMPARE(lines->rowCount(), 1);

        // Enter in the cell leaves the focus ready for the next code
        auto editor = qobject_cast<QLineEdit *>(lines->indexWidget(lines->currentIndex()));
        QVERIFY(editor);
        QTest::keyClicks(editor, QStringLiteral("3"));
        QTest::keyClick(editor, Qt::Key_Return);
        QTest::qWait(20);                                       // Qt commits an Enter on the next tick
        QCOMPARE(lines->item(0, 2)->text(), QStringLiteral("3"));
        QCOMPARE(dialog->focusWidget(), static_cast<QWidget *>(entry));

        scan();
        QVERIFY(dialog->isVisible());
        QCOMPARE(lines->rowCount(), 2);
        done = true;
        dialog->reject();
    });
    m_page->findChild<QPushButton *>(QStringLiteral("newPurchaseButton"))->click();
    QVERIFY(done);
    QCOMPARE(m_page->findChild<QTableWidget *>(QStringLiteral("purchasesTable"))->rowCount(), 0);
    QCOMPARE(m_core.products->product(QStringLiteral("7840002"))->stock, Quantity(0));
}

// --- the till ----------------------------------------------------------------

/// Signed in as the cashier, on the real sales page, with two articles.
bool tst_Modules::showSales()
{
    openUsers();
    m_core.cash->setUsers(m_users.get());
    m_core.sales->setUsers(m_users.get());
    auto factory = qobject_cast<PermissionProvider *>(load(QStringLiteral("modulo_sales_module.dll")));
    if (!factory || !m_users->registerPermissions(factory->permissions()))
        return false;
    if (!m_users->login(QStringLiteral("cajero"), QStringLiteral("clave123")))
        return false;
    for (const auto &article : { std::make_pair("7840001", Money(11000)), std::make_pair("7840002", Money(4950)) }) {
        Product p;
        p.code = QLatin1String(article.first);
        p.description = QStringLiteral("ARTICULO ") + p.code;
        p.price = article.second;
        p.cost = article.second / 2;
        p.stock = AguaraQuantity::whole(10);
        if (!m_core.products->saveProduct(p))
            return false;
    }
    m_page = dynamic_cast<WidgetModuleInterface *>(factory)->createWidget();
    m_page->resize(1100, 700);
    m_page->show();
    return QTest::qWaitForWindowExposed(m_page);
}

bool tst_Modules::openTill(Money hundredThousands)
{
    whenDialogOpens([=](QDialog *count) {
        count->findChild<QSpinBox *>(QStringLiteral("countSpin_100000"))->setValue(int(hundredThousands));
        count->accept();
    });
    m_page->findChild<QPushButton *>(QStringLiteral("closedButton"))->click();
    return m_core.cash->currentSession().has_value();
}

void tst_Modules::scan(const QString &text)
{
    QTest::keyClicks(saleEntry(), text);
    QTest::keyClick(saleEntry(), Qt::Key_Return);
}

void tst_Modules::press(Qt::Key key)
{
    QTest::keyClick(saleEntry(), key);
}

void tst_Modules::aDayAtTheTill()
{
    QVERIFY(showSales());
    QCOMPARE(label("closedTitle")->text(), QStringLiteral("La caja está cerrada"));
    QSignalSpy keepOpen(m_page, SIGNAL(keepOpenChanged()));
    QVERIFY(openTill(1));
    QCOMPARE(keepOpen.count(), 1);                      // the window hears that it must stay open

    scan(QStringLiteral("7840001"));
    scan(QStringLiteral("7840002"));
    QCOMPARE(saleLines()->rowCount(), 2);
    QCOMPARE(label("saleTotal")->text(), QStringLiteral("15.950"));

    whenDialogOpens([](QDialog *payment) {
        QCOMPARE(payment->findChild<QLabel *>(QStringLiteral("paymentTotal"))->text(), QStringLiteral("Gs. 15.950"));
        auto tendered = payment->findChild<QLineEdit *>(QStringLiteral("paymentTendered"));
        QTest::keyClicks(tendered, QStringLiteral("20.000"));
        QCOMPARE(payment->findChild<QLabel *>(QStringLiteral("paymentChange"))->text(), QStringLiteral("Vuelto: Gs. 4.050"));
        QTest::keyClick(tendered, Qt::Key_Return);
    });
    press(Qt::Key_F12);

    QCOMPARE(saleLines()->rowCount(), 0);
    QCOMPARE(label("saleTotal")->text(), QStringLiteral("0"));
    QCOMPARE(label("lastChange")->text(), QStringLiteral("Vuelto: Gs. 4.050"));
    QVERIFY(label("saleMessage")->text().contains(QStringLiteral("001-000001")));
    QCOMPARE(m_core.products->product(QStringLiteral("7840001"))->stock, AguaraQuantity::whole(9));

    // closing: 100.000 of float + 15.950 of sales
    const qint64 sessionId = m_core.cash->currentSession()->id;
    bool closedAsked = false;
    whenDialogOpens([&](QDialog *count) {
        count->findChild<QSpinBox *>(QStringLiteral("countSpin_100000"))->setValue(1);
        count->findChild<QSpinBox *>(QStringLiteral("countSpin_10000"))->setValue(1);
        count->findChild<QSpinBox *>(QStringLiteral("countSpin_5000"))->setValue(1);
        count->findChild<QSpinBox *>(QStringLiteral("countSpin_500"))->setValue(1);
        count->findChild<QSpinBox *>(QStringLiteral("countSpin_100"))->setValue(4);
        count->findChild<QSpinBox *>(QStringLiteral("countSpin_50"))->setValue(1);
        QCOMPARE(count->findChild<QLabel *>(QStringLiteral("countTotal"))->text(), QStringLiteral("Total: Gs. 115.950"));
        whenDialogOpens([&](QDialog *done) {
            closedAsked = true;
            done->reject();                     // no print preview in a test
        });
        count->accept();
    });
    trigger("actionCloseRegister");
    QVERIFY(closedAsked);
    QVERIFY(!m_core.cash->currentSession());
    QCOMPARE(m_core.cash->sessionReport(sessionId)->session.difference, Money(0));
    QCOMPARE(m_core.cash->sessionReport(sessionId)->salesTotal, Money(15950));
    QCOMPARE(label("closedTitle")->text(), QStringLiteral("La caja está cerrada"));
}

void tst_Modules::scanningProblemsStayOnThePage()
{
    QVERIFY(showSales());
    QVERIFY(openTill());
    scan(QStringLiteral("999999"));                     // no dialog in the way of the next scan
    QVERIFY(!QApplication::activeModalWidget());
    QVERIFY(label("saleMessage")->text().contains(QStringLiteral("999999")));
    scan(QStringLiteral("0*7840001"));
    QVERIFY(label("saleMessage")->text().contains(QStringLiteral("no es válida")));
    scan(QStringLiteral("2,5*7840001"));                // sold by the unit
    QVERIFY(label("saleMessage")->text().contains(QStringLiteral("por unidad")));
    QCOMPARE(saleLines()->rowCount(), 0);

    scan(QStringLiteral("7840001"));
    QCOMPARE(saleLines()->rowCount(), 1);
    QVERIFY(label("saleMessage")->isHidden());

    scan(QStringLiteral("10*7840001"));                 // more than there is: a warning, not a refusal
    QCOMPARE(saleLines()->item(0, 2)->text(), QStringLiteral("11"));
    QVERIFY(label("saleMessage")->text().contains(QStringLiteral("-1")));
}

void tst_Modules::quantityPrefixAndRepeatedScans()
{
    QVERIFY(showSales());
    QVERIFY(openTill());
    scan(QStringLiteral("3*7840001"));
    scan(QStringLiteral("7840001"));
    QCOMPARE(saleLines()->rowCount(), 1);
    QCOMPARE(saleLines()->item(0, 2)->text(), QStringLiteral("4"));
    QCOMPARE(label("saleTotal")->text(), QStringLiteral("44.000"));

    whenDialogOpens([](QDialog *dialog) {
        auto edit = dialog->findChild<QLineEdit *>(QStringLiteral("quantityEdit"));
        edit->setText(QStringLiteral("2"));
        dialog->accept();
    });
    press(Qt::Key_F2);
    QCOMPARE(label("saleTotal")->text(), QStringLiteral("22.000"));

    press(Qt::Key_Delete);
    QCOMPARE(saleLines()->rowCount(), 0);
    bool found = false;
    for (const AuditEntry &e : m_users->auditEntries())
        found = found || e.action == QLatin1String("venta.quitar_linea");
    QVERIFY(found);
}

void tst_Modules::scaleLabelsAtTheTill()
{
    QVERIFY(showSales());
    Product weighed;
    weighed.code = QStringLiteral("2000101");
    weighed.description = QStringLiteral("MANDIOCA X KG");
    weighed.price = 4500;
    weighed.unit = m_core.products->units().at(1);
    QVERIFY(m_core.products->saveProduct(weighed));
    weighed.code = QStringLiteral("2800111");
    weighed.description = QStringLiteral("QUESO X KG");
    weighed.price = 48000;
    QVERIFY(m_core.products->saveProduct(weighed));
    QVERIFY(openTill());

    scan(QStringLiteral("2000101012504"));              // 1,250 kg
    QCOMPARE(saleLines()->item(0, 2)->text(), QStringLiteral("1,250 kg"));
    QCOMPARE(saleLines()->item(0, 5)->text(), QStringLiteral("5.625"));
    QCOMPARE(label("saleTotal")->text(), QStringLiteral("5.625"));      // the exact amount owed

    // only paying in cash rounds it, and the dialog says so
    whenDialogOpens([](QDialog *payment) {
        QCOMPARE(payment->findChild<QLabel *>(QStringLiteral("paymentTotal"))->text(), QStringLiteral("Gs. 5.650"));
        QVERIFY(payment->findChild<QLabel *>(QStringLiteral("paymentRounding"))->text().contains(QStringLiteral("Gs. 5.625")));
        auto tendered = payment->findChild<QLineEdit *>(QStringLiteral("paymentTendered"));
        QTest::keyClicks(tendered, QStringLiteral("10.000"));
        QTest::keyClick(tendered, Qt::Key_Return);
    });
    press(Qt::Key_F12);
    QCOMPARE(label("lastChange")->text(), QStringLiteral("Vuelto: Gs. 4.350"));

    scan(QStringLiteral("2800111345001"));              // the label says 34.500
    QCOMPARE(saleLines()->rowCount(), 1);
    QCOMPARE(saleLines()->item(0, 5)->text(), QStringLiteral("34.500"));
}

void tst_Modules::wrongCountIsBlindAndNeedsASupervisor()
{
    QVERIFY(showSales());
    QVERIFY(openTill(1));
    const qint64 sessionId = m_core.cash->currentSession()->id;
    int asked = 0;
    m_users->setSupervisorPrompt([&](const QString &, const QString &, const QString &, QWidget *,
                                     QString *username, QString *password) {
        ++asked;
        *username = QStringLiteral("encargado");
        *password = QStringLiteral("clave123");
        return true;
    });

    QString blindText;
    QString seenDifference;
    whenDialogOpens([&](QDialog *count) {
        count->findChild<QSpinBox *>(QStringLiteral("countSpin_50000"))->setValue(1);   // 50.000 short
        whenDialogOpens([&](QDialog *mismatch) {
            for (const QLabel *l : mismatch->findChildren<QLabel *>())
                blindText += l->text();
            whenDialogOpens([&](QDialog *recount) {
                // the second count starts from the first
                QCOMPARE(recount->findChild<QSpinBox *>(QStringLiteral("countSpin_50000"))->value(), 1);
                whenDialogOpens([&](QDialog *again) {
                    whenDialogOpens([&](QDialog *difference) {
                        seenDifference = difference->findChild<QLabel *>(QStringLiteral("differenceAmount"))->text();
                        auto buttons = difference->findChild<QDialogButtonBox *>();
                        QVERIFY(!buttons->button(QDialogButtonBox::Ok)->isEnabled());   // the reason is required
                        difference->findChild<QLineEdit *>(QStringLiteral("differenceReason"))->setText(QStringLiteral("Vuelto mal dado"));
                        whenDialogOpens([](QDialog *done) { done->reject(); });
                        buttons->button(QDialogButtonBox::Ok)->click();
                    });
                    again->findChild<QPushButton *>(QStringLiteral("mismatchSupervisor"))->click();
                });
                recount->accept();
            });
            mismatch->findChild<QPushButton *>(QStringLiteral("mismatchRecount"))->click();
        });
        count->accept();
    });
    trigger("actionCloseRegister");

    QVERIFY(!blindText.isEmpty());
    QVERIFY2(!blindText.contains(QStringLiteral("100.000")) && !blindText.contains(QStringLiteral("50.000")),
             qPrintable(blindText));
    QCOMPARE(asked, 1);
    QCOMPARE(seenDifference, QStringLiteral("Gs. -50.000"));
    const CashSession session = m_core.cash->sessionReport(sessionId)->session;
    QVERIFY(!session.open);
    QCOMPARE(session.difference, Money(-50000));
    QCOMPARE(session.differenceReason, QStringLiteral("Vuelto mal dado"));
    QCOMPARE(session.authorizedBy, QStringLiteral("encargado"));
    QCOMPARE(session.closedBy, QStringLiteral("cajero"));
}

void tst_Modules::discountOverTheLimitNeedsASupervisor()
{
    QVERIFY(showSales());
    QVERIFY(openTill());
    int asked = 0;
    m_users->setSupervisorPrompt([&](const QString &, const QString &, const QString &, QWidget *, QString *, QString *) {
        ++asked;
        return false;
    });
    scan(QStringLiteral("7840001"));                    // 11.000

    const auto discountOf = [](const QString &percent) {
        whenDialogOpens([=](QDialog *dialog) {
            dialog->findChild<QLineEdit *>(QStringLiteral("discountEdit"))->setText(percent);
            dialog->accept();
        });
    };
    discountOf(QStringLiteral("5"));                    // a cashier's limit
    press(Qt::Key_F4);
    QCOMPARE(asked, 0);
    QCOMPARE(saleLines()->item(0, 4)->text(), QStringLiteral("550"));

    discountOf(QStringLiteral("10"));
    press(Qt::Key_F4);
    QCOMPARE(asked, 1);
    QCOMPARE(saleLines()->item(0, 4)->text(), QStringLiteral("550"));       // refused: as it was
    QVERIFY(label("saleMessage")->text().contains(QStringLiteral("no se autorizó")));

    discountOf(QStringLiteral("5"));                    // on top of the line's: 10 % of the sale
    press(Qt::Key_F5);
    QCOMPARE(asked, 2);
    QCOMPARE(label("saleDiscount")->text(), QStringLiteral("550"));
}

void tst_Modules::voidingFromTheScreen()
{
    QVERIFY(showSales());
    QVERIFY(openTill());
    m_users->setSupervisorPrompt([](const QString &, const QString &, const QString &, QWidget *,
                                    QString *username, QString *password) {
        *username = QStringLiteral("encargado");
        *password = QStringLiteral("clave123");
        return true;
    });
    scan(QStringLiteral("2*7840001"));
    whenDialogOpens([](QDialog *payment) { payment->accept(); });
    press(Qt::Key_F12);
    QCOMPARE(m_core.products->product(QStringLiteral("7840001"))->stock, AguaraQuantity::whole(8));

    whenDialogOpens([](QDialog *list) {
        QCOMPARE(list->findChild<QTableWidget *>(QStringLiteral("sessionSalesTable"))->rowCount(), 1);
        whenDialogOpens([list](QDialog *reason) {
            reason->findChild<QLineEdit *>(QStringLiteral("reasonEdit"))->setText(QStringLiteral("Error de cobro"));
            reason->accept();
            QTimer::singleShot(50, list, [list]() {
                QCOMPARE(list->findChild<QTableWidget *>(QStringLiteral("sessionSalesTable"))->item(0, 4)->text(),
                         QStringLiteral("Anulada"));
                list->reject();
            });
        });
        list->findChild<QPushButton *>(QStringLiteral("voidSaleButton"))->click();
    });
    trigger("actionSessionSales");

    QCOMPARE(m_core.products->product(QStringLiteral("7840001"))->stock, AguaraQuantity::whole(10));
    QCOMPARE(m_core.cash->sessionReport(m_core.cash->currentSession()->id)->expectedCash, Money(100000));
}

static void fillVoucher(QDialog *voucher, const QString &code, const QString &amount = QString())
{
    if (!amount.isEmpty())
        voucher->findChild<QLineEdit *>(QStringLiteral("voucherAmount"))->setText(amount);
    auto reference = voucher->findChild<QLineEdit *>(QStringLiteral("voucherReference"));
    reference->setText(code);
    QTest::keyClick(reference, Qt::Key_Return);
}

void tst_Modules::cardPaysEverything()
{
    QVERIFY(showSales());
    QVERIFY(openTill());
    scan(QStringLiteral("7840001"));                    // 11.000

    whenDialogOpens([](QDialog *payment) {
        whenDialogOpens([](QDialog *voucher) {
            auto reference = voucher->findChild<QLineEdit *>(QStringLiteral("voucherReference"));
            QCOMPARE(voucher->findChild<QLineEdit *>(QStringLiteral("voucherAmount"))->text(), QStringLiteral("11.000"));
            QTest::keyClick(reference, Qt::Key_Return);         // no code: the code is the proof
            QVERIFY(voucher->isVisible());
            QVERIFY(!voucher->findChild<QLabel *>(QStringLiteral("voucherError"))->text().isEmpty());
            fillVoucher(voucher, QStringLiteral("448811"));
        });
        auto tendered = payment->findChild<QLineEdit *>(QStringLiteral("paymentTendered"));
        QTest::keyClick(tendered, Qt::Key_F2);
        QCOMPARE(payment->findChild<QLabel *>(QStringLiteral("paymentCaption"))->text(), QStringLiteral("Pago completo"));
        QCOMPARE(payment->findChild<QLabel *>(QStringLiteral("paymentTotal"))->text(), QStringLiteral("Gs. 0"));
        QVERIFY(!tendered->isEnabled());
        QTest::keyClick(payment, Qt::Key_Return);
    });
    press(Qt::Key_F12);

    QCOMPARE(saleLines()->rowCount(), 0);
    QVERIFY(label("lastChange")->isHidden());           // no cash, no change
    const auto report = m_core.cash->sessionReport(m_core.cash->currentSession()->id);
    QCOMPARE(report->salesTotal, Money(11000));
    QCOMPARE(report->expectedCash, Money(100000));
    QCOMPARE(report->vouchers.size(), 1);
    const QString html = ClosingSheet::html(*report);
    QVERIFY(html.contains(QStringLiteral("Comprobantes por conciliar")));
    QVERIFY(html.contains(QStringLiteral("Tarjeta de débito - 448811 - 001-000001")));
}

void tst_Modules::cardAndCashTogether()
{
    QVERIFY(showSales());
    QVERIFY(openTill());
    scan(QStringLiteral("7840001"));
    scan(QStringLiteral("7840002"));                    // 15.950

    whenDialogOpens([](QDialog *payment) {
        whenDialogOpens([](QDialog *voucher) { fillVoucher(voucher, QStringLiteral("t-77"), QStringLiteral("10.975")); });
        auto tendered = payment->findChild<QLineEdit *>(QStringLiteral("paymentTendered"));
        QTest::keyClick(tendered, Qt::Key_F4);
        QCOMPARE(payment->findChild<QTableWidget *>(QStringLiteral("paymentVouchers"))->item(0, 1)->text(), QStringLiteral("T-77"));
        // 4.975 left: only this part rounds
        QCOMPARE(payment->findChild<QLabel *>(QStringLiteral("paymentCaption"))->text(), QStringLiteral("Falta pagar en efectivo"));
        QCOMPARE(payment->findChild<QLabel *>(QStringLiteral("paymentTotal"))->text(), QStringLiteral("Gs. 5.000"));
        QVERIFY(payment->findChild<QLabel *>(QStringLiteral("paymentRounding"))->text().contains(QStringLiteral("Gs. 4.975")));
        QTest::keyClicks(tendered, QStringLiteral("10.000"));
        QTest::keyClick(tendered, Qt::Key_Return);
    });
    press(Qt::Key_F12);

    QCOMPARE(label("lastChange")->text(), QStringLiteral("Vuelto: Gs. 5.000"));
    const auto report = m_core.cash->sessionReport(m_core.cash->currentSession()->id);
    QCOMPARE(report->salesTotal, Money(15975));
    QCOMPARE(report->rounding, Money(25));
    QCOMPARE(report->expectedCash, Money(105000));
}

void tst_Modules::cancellingWarnsAboutVouchers()
{
    QVERIFY(showSales());
    QVERIFY(openTill());
    scan(QStringLiteral("7840001"));

    whenDialogOpens([](QDialog *payment) {
        whenDialogOpens([](QDialog *voucher) { fillVoucher(voucher, QStringLiteral("1")); });
        QTest::keyClick(payment, Qt::Key_F3);

        whenDialogOpens([](QDialog *confirm) { confirm->reject(); });
        payment->reject();
        QVERIFY(payment->isVisible());                  // a "no" keeps the voucher

        whenDialogOpens([](QDialog *confirm) { confirm->accept(); });
        payment->reject();
        QVERIFY(!payment->isVisible());
    });
    press(Qt::Key_F12);

    QCOMPARE(saleLines()->rowCount(), 1);               // not charged: the sale stays
    QCOMPARE(m_core.sales->sessionSales(m_core.cash->currentSession()->id).size(), 0);
}

void tst_Modules::repeatedVoucherNeedsASupervisor()
{
    QVERIFY(showSales());
    QVERIFY(openTill());
    SaleDraft draft;
    SaleLine line;
    line.code = QStringLiteral("7840001");
    line.description = QStringLiteral("ARTICULO");
    line.unitPrice = 11000;
    draft.lines << line;
    QVERIFY(m_core.sales->checkout(draft, { byVoucher(PaymentMethod::Debit, 11000, QStringLiteral("777")) }));

    int asked = 0;
    m_users->setSupervisorPrompt([&](const QString &, const QString &reason, const QString &, QWidget *,
                                     QString *username, QString *password) {
        if (++asked == 1)
            return false;
        [&] { QVERIFY(reason.contains(QStringLiteral("001-000001"))); }();
        *username = QStringLiteral("encargado");
        *password = QStringLiteral("clave123");
        return true;
    });
    scan(QStringLiteral("7840001"));
    whenDialogOpens([](QDialog *payment) {
        whenDialogOpens([](QDialog *voucher) {
            fillVoucher(voucher, QStringLiteral("777"));        // refused
            QVERIFY(voucher->isVisible());
            QVERIFY(voucher->findChild<QLabel *>(QStringLiteral("voucherError"))->text().contains(QStringLiteral("001-000001")));
            fillVoucher(voucher, QStringLiteral("777"));        // authorized
        });
        QTest::keyClick(payment, Qt::Key_F2);
        QTest::keyClick(payment, Qt::Key_Return);
    });
    press(Qt::Key_F12);

    QCOMPARE(asked, 2);
    QCOMPARE(saleLines()->rowCount(), 0);
    bool audited = false;
    for (const AuditEntry &e : m_users->auditEntries())
        audited = audited || e.action == QLatin1String("venta.comprobante_repetido");
    QVERIFY(audited);
}

void tst_Modules::voidingACardSaysToReverseIt()
{
    QVERIFY(showSales());
    QVERIFY(openTill());
    m_users->setSupervisorPrompt([](const QString &, const QString &, const QString &, QWidget *,
                                    QString *username, QString *password) {
        *username = QStringLiteral("encargado");
        *password = QStringLiteral("clave123");
        return true;
    });
    SaleDraft draft;
    SaleLine line;
    line.code = QStringLiteral("7840001");
    line.description = QStringLiteral("ARTICULO");
    line.unitPrice = 11000;
    draft.lines << line;
    QVERIFY(m_core.sales->checkout(draft, { byVoucher(PaymentMethod::Credit, 11000, QStringLiteral("C9")) }));

    QString shown;
    whenDialogOpens([&shown](QDialog *list) {
        whenDialogOpens([&shown, list](QDialog *reason) {
            for (const QLabel *text : reason->findChildren<QLabel *>())
                shown += text->text() + QLatin1Char('\n');
            reason->reject();
            QTimer::singleShot(50, list, [list]() { list->reject(); });
        });
        list->findChild<QPushButton *>(QStringLiteral("voidSaleButton"))->click();
    });
    trigger("actionSessionSales");

    QVERIFY2(shown.contains(QStringLiteral("Tarjeta de crédito C9")), qPrintable(shown));
    QVERIFY2(!shown.contains(QStringLiteral("de la caja")), qPrintable(shown));     // no cash leaves
}

void tst_Modules::outageOnlyFinishesTheSaleInProgress()
{
    QVERIFY(showSales());
    QVERIFY(openTill());
    scan(QStringLiteral("7840001"));
    QCOMPARE(saleLines()->rowCount(), 1);

    m_core.monitor->simulateOutage(true);
    QVERIFY(TestSupport::waitFor([&] { return !label("saleConnection")->isHidden(); }, 2500));
    QVERIFY(label("saleConnection")->text().contains(QStringLiteral("Sin conexión")));

    // nothing more goes in
    scan(QStringLiteral("7840002"));
    QCOMPARE(saleLines()->rowCount(), 1);
    QVERIFY(label("saleMessage")->text().contains(QStringLiteral("cobre la venta en curso")));

    // the sale on the screen is finished
    whenDialogOpens([](QDialog *payment) {
        auto tendered = payment->findChild<QLineEdit *>(QStringLiteral("paymentTendered"));
        QTest::keyClicks(tendered, QStringLiteral("20.000"));
        QTest::keyClick(tendered, Qt::Key_Return);
    });
    press(Qt::Key_F12);
    QCOMPARE(saleLines()->rowCount(), 0);
    QCOMPARE(label("lastChange")->text(), QStringLiteral("Vuelto: Gs. 9.000"));
    QCOMPARE(label("saleMessage")->text(), QStringLiteral("Venta 001-000001 cobrada."));
    QCOMPARE(label("closedTitle")->isVisible(), false);         // the till doesn't look closed

    // and no new one starts
    scan(QStringLiteral("7840001"));
    QCOMPARE(saleLines()->rowCount(), 0);
    QVERIFY(label("saleMessage")->text().contains(QStringLiteral("no se pueden iniciar")));

    // the list says what waits
    whenDialogOpens([](QDialog *list) {
        QCOMPARE(list->findChild<QTableWidget *>(QStringLiteral("pendingTable"))->rowCount(), 1);
        list->reject();
    });
    trigger("actionPending");

    m_core.monitor->simulateOutage(false);
    QVERIFY(TestSupport::waitFor([&] { return m_core.sales->pendingSales().isEmpty(); }));
    QVERIFY(TestSupport::waitFor([&] { return label("saleConnection")->isHidden(); }, 2500));
    QCOMPARE(m_core.sales->sessionSales(m_core.cash->currentSession()->id).size(), 1);
    scan(QStringLiteral("7840001"));
    QCOMPARE(saleLines()->rowCount(), 1);               // selling again
}

void tst_Modules::customerOfTheSale()
{
    QVERIFY(showSales());
    QVERIFY(openTill());
    Customer rosa;
    rosa.name = QStringLiteral("DOÑA ROSA");
    QVERIFY(m_core.customers->saveCustomer(rosa));
    QCOMPARE(label("saleCustomer")->text(), QStringLiteral("Consumidor final"));

    whenDialogOpens([](QDialog *pick) {
        QTest::keyClicks(pick->findChild<QLineEdit *>(QStringLiteral("customerFilter")), QStringLiteral("rosa"));
        QCOMPARE(pick->findChild<QTableWidget *>(QStringLiteral("customerPickTable"))->rowCount(), 1);
        pick->accept();
    });
    press(Qt::Key_F8);
    QCOMPARE(label("saleCustomer")->text(), QStringLiteral("DOÑA ROSA"));

    scan(QStringLiteral("7840001"));
    whenDialogOpens([](QDialog *payment) { payment->accept(); });
    press(Qt::Key_F12);
    QCOMPARE(m_core.sales->sessionSales(m_core.cash->currentSession()->id).first().customerName, QStringLiteral("DOÑA ROSA"));
    QCOMPARE(label("saleCustomer")->text(), QStringLiteral("Consumidor final"));    // the next sale starts clean
}

/// Up to the point of the sale; the cashier has charged 2 of the 7840001.
static void chargeTwo(QDialog *payment)
{
    payment->accept();
}

void tst_Modules::returnThroughTheScreen()
{
    QVERIFY(showSales());
    QVERIFY(openTill());
    m_users->setSupervisorPrompt([](const QString &, const QString &, const QString &, QWidget *,
                                    QString *username, QString *password) {
        *username = QStringLiteral("encargado");
        *password = QStringLiteral("clave123");
        return true;
    });
    scan(QStringLiteral("2*7840001"));
    whenDialogOpens(chargeTwo);
    press(Qt::Key_F12);
    QCOMPARE(m_core.products->product(QStringLiteral("7840001"))->stock, AguaraQuantity::whole(8));

    whenDialogOpens([](QDialog *dialog) {
        dialog->findChild<QLineEdit *>(QStringLiteral("returnNumber"))->setText(QStringLiteral("001-000099"));
        dialog->accept();                               // Enter: it looks the sale up
        QVERIFY(dialog->isVisible());
        QVERIFY(dialog->findChild<QLabel *>(QStringLiteral("returnError"))->text().contains(QStringLiteral("ninguna")));

        dialog->findChild<QLineEdit *>(QStringLiteral("returnNumber"))->setText(QStringLiteral("001-000001"));
        dialog->accept();
        QVERIFY(dialog->isVisible());
        auto lines = dialog->findChild<QTableWidget *>(QStringLiteral("returnLines"));
        QCOMPARE(lines->rowCount(), 1);
        lines->item(0, 3)->setText(QStringLiteral("1"));
        QCOMPARE(dialog->findChild<QLabel *>(QStringLiteral("returnAmount"))->text(), QStringLiteral("A devolver: Gs. 11.000"));

        dialog->accept();                               // no reason yet
        QVERIFY(dialog->isVisible());
        dialog->findChild<QLineEdit *>(QStringLiteral("returnReason"))->setText(QStringLiteral("Vencido"));
        dialog->accept();
    });
    trigger("actionReturn");

    QCOMPARE(m_core.products->product(QStringLiteral("7840001"))->stock, AguaraQuantity::whole(9));
    QVERIFY(label("saleMessage")->text().contains(QStringLiteral("entregue Gs. 11.000")));
    const auto report = m_core.cash->sessionReport(m_core.cash->currentSession()->id);
    QCOMPARE(report->returns.first().authorizedBy, QStringLiteral("encargado"));
    QVERIFY(ClosingSheet::html(*report).contains(QStringLiteral("Devoluciones")));
}

void tst_Modules::returnNeedsPermission()
{
    QVERIFY(showSales());
    QVERIFY(openTill());
    int asked = 0;
    m_users->setSupervisorPrompt([&](const QString &, const QString &, const QString &, QWidget *, QString *, QString *) {
        ++asked;
        return false;
    });
    trigger("actionReturn");                            // no dialog opens: the test would hang on it
    QCOMPARE(asked, 1);
}

void tst_Modules::creditNoteThroughThePaymentScreen()
{
    QVERIFY(showSales());
    QVERIFY(openTill());
    scan(QStringLiteral("7840001"));
    whenDialogOpens([](QDialog *payment) { payment->accept(); });
    press(Qt::Key_F12);

    ReturnRequest r;
    r.saleId = m_core.sales->sessionSales(m_core.cash->currentSession()->id).first().id;
    ReturnLine line;
    line.line = 1;
    line.quantity = AguaraQuantity::One;
    r.lines << line;
    r.method = QString::fromLatin1(PaymentMethod::CreditNote);
    r.reason = QStringLiteral("Cambio");
    QVERIFY(m_core.returns->processReturn(r));

    scan(QStringLiteral("7840001"));
    scan(QStringLiteral("7840002"));                    // 15.950: the note pays 11.000
    whenDialogOpens([](QDialog *payment) {
        whenDialogOpens([](QDialog *voucher) {
            auto reference = voucher->findChild<QLineEdit *>(QStringLiteral("voucherReference"));
            reference->setText(QStringLiteral("NC-000099"));
            QTest::keyClick(reference, Qt::Key_Return);
            QVERIFY(voucher->isVisible());              // doesn't exist
            reference->setText(QStringLiteral("nc-000001"));
            QTest::keyClick(reference, Qt::Key_Return);
        });
        // the fifth method, after debit, credit, transfer and QR: F7
        QTest::keyClick(payment->findChild<QLineEdit *>(QStringLiteral("paymentTendered")), Qt::Key_F7);
        QCOMPARE(payment->findChild<QTableWidget *>(QStringLiteral("paymentVouchers"))->item(0, 2)->text(), QStringLiteral("11.000"));
        QCOMPARE(payment->findChild<QLabel *>(QStringLiteral("paymentTotal"))->text(), QStringLiteral("Gs. 4.950"));
        payment->accept();
    });
    press(Qt::Key_F12);

    QCOMPARE(saleLines()->rowCount(), 0);
    QCOMPARE(m_core.returns->creditNote(QStringLiteral("NC-000001"))->balance, Money(0));
}

void tst_Modules::paymentKeysFollowTheMethods()
{
    QVERIFY(showSales());
    QVERIFY(openTill());
    PaymentMethodInfo debit;
    debit.code = QString::fromLatin1(PaymentMethod::Debit);
    debit.active = false;
    QVERIFY(m_core.sales->savePaymentMethod(debit));
    PaymentMethodInfo tigo;
    tigo.code = QStringLiteral("Tigo Money");
    tigo.needsCode = false;
    QVERIFY(m_core.sales->savePaymentMethod(tigo));

    scan(QStringLiteral("7840001"));
    whenDialogOpens([](QDialog *payment) {
        QVERIFY(!payment->findChild<QPushButton *>(QStringLiteral("paymentMethod_tarjeta_debito")));
        auto tigoButton = payment->findChild<QPushButton *>(QStringLiteral("paymentMethod_Tigo Money"));
        QVERIFY(tigoButton);
        QVERIFY(tigoButton->text().contains(QStringLiteral("Tigo Money")));
        whenDialogOpens([](QDialog *voucher) {
            QVERIFY(voucher->findChild<QLineEdit *>(QStringLiteral("voucherReference"))->isHidden());
            voucher->accept();                          // no code asked
        });
        tigoButton->click();
        QCOMPARE(payment->findChild<QLabel *>(QStringLiteral("paymentCaption"))->text(), QStringLiteral("Pago completo"));
        payment->accept();
    });
    press(Qt::Key_F12);
    QCOMPARE(saleLines()->rowCount(), 0);
}

void tst_Modules::configuringPaymentMethods()
{
    QVERIFY(showSales());
    QVERIFY(openTill());
    m_users->setSupervisorPrompt([](const QString &, const QString &, const QString &, QWidget *,
                                    QString *username, QString *password) {
        *username = QStringLiteral("encargado");
        *password = QStringLiteral("clave123");
        return true;
    });
    whenDialogOpens([](QDialog *dialog) {
        auto table = dialog->findChild<QTableWidget *>(QStringLiteral("methodsTable"));
        QCOMPARE(table->rowCount(), 4);
        table->item(3, 2)->setCheckState(Qt::Unchecked);        // QR out of use
        dialog->findChild<QLineEdit *>(QStringLiteral("methodsNew"))->setText(QStringLiteral("Cheque"));
        dialog->findChild<QPushButton *>(QStringLiteral("methodsAdd"))->click();
        QCOMPARE(table->rowCount(), 5);
        dialog->accept();
    });
    trigger("actionPaymentMethods");

    QStringList codes;
    for (const PaymentMethodInfo &m : m_core.sales->paymentMethods())
        codes << m.code;
    QVERIFY(codes.contains(QStringLiteral("Cheque")));
    QVERIFY(!codes.contains(QStringLiteral("qr")));
}

void tst_Modules::customersPage()
{
    openUsers();
    auto factory = load(QStringLiteral("modulo_customers.dll"));
    auto provider = qobject_cast<PermissionProvider *>(factory);
    QVERIFY(provider);
    QVERIFY(m_users->registerPermissions(provider->permissions()));
    auto widgets = qobject_cast<WidgetModuleInterface *>(factory);
    QVERIFY(widgets);
    QVERIFY(m_users->login(QStringLiteral("cajero"), QStringLiteral("clave123")));
    m_page = widgets->createWidget();
    m_page->show();

    whenDialogOpens([](QDialog *dialog) {
        auto name = dialog->findChild<QLineEdit *>(QStringLiteral("customerName"));
        name->setText(QStringLiteral("ANDE"));
        auto type = dialog->findChild<QComboBox *>(QStringLiteral("customerType"));
        type->setCurrentIndex(1);
        auto document = dialog->findChild<QLineEdit *>(QStringLiteral("customerDocument"));
        document->setText(QStringLiteral("80009735-2"));
        dialog->accept();
        QVERIFY(dialog->isVisible());                   // a wrong digit: it says so and stays
        QVERIFY(dialog->findChild<QLabel *>(QStringLiteral("customerError"))->text().contains(QStringLiteral("RUC")));
        document->setText(QStringLiteral("80009735-1"));
        dialog->accept();
    });
    m_page->findChild<QPushButton *>(QStringLiteral("customerAdd"))->click();

    auto table = m_page->findChild<QTableWidget *>(QStringLiteral("customerTable"));
    QCOMPARE(table->rowCount(), 1);
    QCOMPARE(table->item(0, 1)->text(), QStringLiteral("80009735-1"));

    // the cashier adds them, but only a supervisor removes them
    int asked = 0;
    m_users->setSupervisorPrompt([&](const QString &, const QString &, const QString &, QWidget *, QString *, QString *) {
        ++asked;
        return false;
    });
    m_page->findChild<QPushButton *>(QStringLiteral("customerToggle"))->click();
    QCOMPARE(asked, 1);
    QCOMPARE(m_core.customers->customers().size(), 1);
}

void tst_Modules::reportsPage()
{
    openUsers();
    auto factory = load(QStringLiteral("modulo_stats.dll"));
    auto provider = qobject_cast<PermissionProvider *>(factory);
    QVERIFY(provider);
    QVERIFY(m_users->registerPermissions(provider->permissions()));
    auto widgets = qobject_cast<WidgetModuleInterface *>(factory);
    QVERIFY(widgets);

    Product p;
    p.code = QStringLiteral("A");
    p.description = QStringLiteral("ARTICULO");
    p.price = 10000;
    p.cost = 7000;
    p.stock = AguaraQuantity::whole(10);
    QVERIFY(m_core.products->saveProduct(p));
    DenominationCount note;
    note.denomination = 100000;
    note.count = 1;
    QVERIFY(m_core.cash->openSession({ note }));
    SaleLine line;
    line.code = p.code;
    line.description = p.description;
    line.unitPrice = p.price;
    SaleDraft draft;
    draft.lines << line;
    QVERIFY(m_core.sales->checkout(draft, { inCash(10000) }) > 0);

    QVERIFY(m_users->login(QStringLiteral("encargado"), QStringLiteral("clave123")));
    m_page = widgets->createWidget();
    m_page->show();

    // the first report runs by itself, today, and its total comes last
    auto table = m_page->findChild<QTableWidget *>(QStringLiteral("reportTable"));
    auto list = m_page->findChild<QListWidget *>(QStringLiteral("reportList"));
    QCOMPARE(m_page->findChild<QLabel *>(QStringLiteral("reportTitle"))->text(), QStringLiteral("Ventas por día"));
    QCOMPARE(table->rowCount(), 2);
    QCOMPARE(table->item(1, 0)->text(), QStringLiteral("Total"));
    QCOMPARE(table->item(1, 3)->text(), QStringLiteral("10.000"));
    QVERIFY(m_page->findChild<QPushButton *>(QStringLiteral("reportCsv"))->isEnabled());

    // headings can't be chosen, and each report shows its own filters
    QCOMPARE(list->item(0)->flags(), Qt::NoItemFlags);
    const auto choose = [list](const QString &title) {
        const auto found = list->findItems(title, Qt::MatchExactly);
        if (!found.isEmpty())
            list->setCurrentItem(found.first());
        return !found.isEmpty();
    };
    QVERIFY(choose(QStringLiteral("Stock valorizado")));
    QVERIFY(m_page->findChild<QWidget *>(QStringLiteral("reportFrom"))->isHidden()
            || !m_page->findChild<QWidget *>(QStringLiteral("reportFrom"))->isVisible());
    QCOMPARE(table->rowCount(), 2);

    // the kardex waits for its code, and a wrong one says so
    QVERIFY(choose(QStringLiteral("Kardex de un artículo")));
    QCOMPARE(table->rowCount(), 0);
    QVERIFY(!m_page->findChild<QPushButton *>(QStringLiteral("reportCsv"))->isEnabled());
    auto text = m_page->findChild<QLineEdit *>(QStringLiteral("reportText"));
    text->setText(QStringLiteral("NADA"));
    QTest::keyClick(text, Qt::Key_Return);
    QVERIFY(m_page->findChild<QLabel *>(QStringLiteral("reportProblem"))->text().contains(QStringLiteral("código")));
    text->setText(QStringLiteral("A"));
    QTest::keyClick(text, Qt::Key_Return);
    QVERIFY(table->rowCount() >= 1);
    QVERIFY(m_page->findChild<QLabel *>(QStringLiteral("reportProblem"))->isHidden());

    // what costs show is for who may see costs
    QVERIFY(m_users->login(QStringLiteral("cajero"), QStringLiteral("clave123")));
    delete m_page;
    m_page = widgets->createWidget();
    m_page->show();
    list = m_page->findChild<QListWidget *>(QStringLiteral("reportList"));
    QVERIFY(list->findItems(QStringLiteral("Margen por artículo"), Qt::MatchExactly).isEmpty());
    QVERIFY(list->findItems(QStringLiteral("Stock valorizado"), Qt::MatchExactly).isEmpty());
    QVERIFY(list->findItems(QStringLiteral("Registro de auditoría"), Qt::MatchExactly).isEmpty());
    QVERIFY(!list->findItems(QStringLiteral("Ventas por día"), Qt::MatchExactly).isEmpty());
}

static PrinterSettings thermal()
{
    PrinterSettings s;
    s.kind = PrinterKind::Thermal;
    s.name = QStringLiteral("TERMICA");
    return s;
}

void tst_Modules::chargingPrintsAndOpensTheDrawer()
{
    QVERIFY(showSales());
    QVERIFY(openTill());
    m_core.print->savePrinterSettings(thermal());
    scan(QStringLiteral("7840001"));
    whenDialogOpens([](QDialog *payment) { payment->accept(); });
    press(Qt::Key_F12);

    QCOMPARE(m_core.printed.size(), 1);
    QVERIFY(m_core.printed.first().contains("ARTICULO 7840001"));
    QVERIFY(m_core.printed.first().contains(QByteArray("\x1B\x70\x00\x19\xFA", 5)));
    QCOMPARE(label("saleMessage")->text(), QStringLiteral("Venta 001-000001 cobrada."));
}

void tst_Modules::printerFailureDoesNotStopTheSale()
{
    QVERIFY(showSales());
    QVERIFY(openTill());
    m_core.print->savePrinterSettings(thermal());
    m_core.printerFails = true;
    scan(QStringLiteral("7840001"));
    whenDialogOpens([](QDialog *payment) { payment->accept(); });
    press(Qt::Key_F12);

    QCOMPARE(saleLines()->rowCount(), 0);               // charged all the same
    QCOMPARE(m_core.sales->sessionSales(m_core.cash->currentSession()->id).size(), 1);
    QVERIFY(label("saleMessage")->text().contains(QStringLiteral("sin ticket")));
    QVERIFY(label("saleMessage")->text().contains(QStringLiteral("no responde")));
}

void tst_Modules::copyNeedsASupervisor()
{
    QVERIFY(showSales());
    QVERIFY(openTill());
    m_core.print->savePrinterSettings(thermal());
    int asked = 0;
    m_users->setSupervisorPrompt([&](const QString &, const QString &, const QString &, QWidget *,
                                     QString *username, QString *password) {
        if (++asked == 1)
            return false;
        *username = QStringLiteral("encargado");
        *password = QStringLiteral("clave123");
        return true;
    });
    scan(QStringLiteral("7840001"));
    whenDialogOpens([](QDialog *payment) { payment->accept(); });
    press(Qt::Key_F12);
    QCOMPARE(m_core.printed.size(), 1);

    whenDialogOpens([](QDialog *list) {
        auto reprint = list->findChild<QPushButton *>(QStringLiteral("printSaleButton"));
        reprint->click();                               // refused
        reprint->click();                               // authorized
        list->reject();
    });
    trigger("actionSessionSales");

    QCOMPARE(asked, 2);
    QCOMPARE(m_core.printed.size(), 2);
    QCOMPARE(m_core.printed.at(1).count("COPIA"), 2);
}

void tst_Modules::drawerByHandNeedsPermission()
{
    QVERIFY(showSales());
    QVERIFY(openTill());
    m_core.print->savePrinterSettings(thermal());
    int asked = 0;
    m_users->setSupervisorPrompt([&](const QString &, const QString &, const QString &, QWidget *,
                                     QString *username, QString *password) {
        if (++asked == 1)
            return false;
        *username = QStringLiteral("encargado");
        *password = QStringLiteral("clave123");
        return true;
    });
    trigger("actionOpenDrawer");                        // the cashier, refused
    QVERIFY(m_core.printed.isEmpty());
    trigger("actionOpenDrawer");
    QCOMPARE(m_core.printed.size(), 1);
    QCOMPARE(asked, 2);
}

void tst_Modules::closingSheetGoesToTheThermalPrinter()
{
    QVERIFY(showSales());
    QVERIFY(openTill());
    m_core.print->savePrinterSettings(thermal());
    QVERIFY(m_core.cash->addMovement(CashMovementType::In, 5000, QStringLiteral("Cambio")));

    // through the screen: the count, then "print the sheet?", and no preview
    whenDialogOpens([](QDialog *count) {
        count->findChild<QSpinBox *>(QStringLiteral("countSpin_100000"))->setValue(1);
        count->findChild<QSpinBox *>(QStringLiteral("countSpin_5000"))->setValue(1);
        whenDialogOpens([](QDialog *ask) { ask->accept(); });
        count->accept();
    });
    trigger("actionCloseRegister");

    QCOMPARE(m_core.printed.size(), 1);
    const QByteArray job = m_core.printed.first();
    QVERIFY(job.contains("Planilla"));
    QVERIFY(job.contains("Cambio"));
    QVERIFY(job.contains("Gs. 105.000"));               // expected
    QVERIFY(!job.contains(QByteArray("\x1B\x70", 2))); // a sheet doesn't open the drawer
}

void tst_Modules::holdAndRecall()
{
    QVERIFY(showSales());
    QVERIFY(openTill());
    scan(QStringLiteral("7840001"));
    press(Qt::Key_F6);
    QCOMPARE(saleLines()->rowCount(), 0);
    QCOMPARE(m_core.sales->heldSales().size(), 1);

    scan(QStringLiteral("7840002"));                    // the next customer
    press(Qt::Key_F7);                                  // refused while a sale is on the screen
    QVERIFY(!QApplication::activeModalWidget());
    whenDialogOpens([](QDialog *payment) { payment->accept(); });
    press(Qt::Key_F12);

    whenDialogOpens([](QDialog *held) {
        QCOMPARE(held->findChild<QTableWidget *>(QStringLiteral("heldTable"))->rowCount(), 1);
        held->accept();
    });
    press(Qt::Key_F7);
    QCOMPARE(saleLines()->rowCount(), 1);
    QCOMPARE(saleLines()->item(0, 0)->text(), QStringLiteral("7840001"));
    QVERIFY(m_core.sales->heldSales().isEmpty());
}

void tst_Modules::forgottenSessionNeedsASupervisor()
{
    QVERIFY(showSales());
    QVERIFY(openTill());
    QSqlQuery q(m_core.connection());
    QVERIFY(q.exec(QStringLiteral("UPDATE sesiones_caja SET apertura = '%1'")
                       .arg(QDateTime::currentDateTime().addDays(-1).toString(Qt::ISODate))));
    m_page->hide();
    m_page->show();                                     // back on the page: it looks again
    QVERIFY(label("closedTitle")->text().contains(QStringLiteral("sin cerrar")));

    int asked = 0;
    m_users->setSupervisorPrompt([&](const QString &, const QString &, const QString &, QWidget *, QString *, QString *) {
        ++asked;
        return false;
    });
    m_page->findChild<QPushButton *>(QStringLiteral("closedButton"))->click();
    QCOMPARE(asked, 1);
    QVERIFY(!QApplication::activeModalWidget());        // no count without the supervisor
    QVERIFY(m_core.cash->currentSession().has_value());
}

void tst_Modules::searchByDescription()
{
    QVERIFY(showSales());
    QVERIFY(openTill());
    whenDialogOpens([](QDialog *search) {
        QCOMPARE(search->findChild<QLineEdit *>(QStringLiteral("articleFilter"))->text(), QStringLiteral("articulo 7840002"));
        search->accept();
    });
    scan(QStringLiteral("articulo 7840002"));
    QCOMPARE(saleLines()->rowCount(), 1);
    QCOMPARE(saleLines()->item(0, 0)->text(), QStringLiteral("7840002"));
    QCOMPARE(saleEntry()->text(), QString());
}

void tst_Modules::closingSheetShowsTheFigures()
{
    QVERIFY(showSales());
    QVERIFY(openTill());
    SaleDraft draft;
    SaleLine line;
    line.code = QStringLiteral("7840001");
    line.description = QStringLiteral("ARTICULO");
    line.unitPrice = 11000;
    draft.lines << line;
    QVERIFY(m_core.sales->checkout(draft, { inCash(20000) }));
    QVERIFY(m_core.cash->addMovement(CashMovementType::Withdrawal, 5000, QStringLiteral("Al <cofre>")));
    DenominationCount count;
    count.denomination = 100000;
    count.count = 1;
    const qint64 id = m_core.cash->currentSession()->id;
    QVERIFY(m_core.cash->closeWithDifference({ count }, QStringLiteral("Faltó plata"), QStringLiteral("encargado")));

    const QString html = ClosingSheet::html(*m_core.cash->sessionReport(id));
    QVERIFY(html.contains(QStringLiteral("Efectivo")));
    QVERIFY(html.contains(QStringLiteral("Gs. 11.000")));           // sold
    QVERIFY(html.contains(QStringLiteral("Gs. 106.000")));          // expected
    QVERIFY(html.contains(QStringLiteral("Gs. -6.000")));           // the difference
    QVERIFY(html.contains(QStringLiteral("Faltó plata")));
    QVERIFY(html.contains(QStringLiteral("Al &lt;cofre&gt;")));     // escaped
}

void tst_Modules::countThroughTheScreen()
{
    QLineEdit *search = nullptr;
    QVERIFY(showInventory(&search));
    QVERIFY(m_core.inventory->adjustStock(QStringLiteral("7840001"), AguaraQuantity::whole(5), QStringLiteral("x")));
    m_page->findChild<QTabWidget *>(QStringLiteral("inventorySections"))->setCurrentIndex(3);

    m_page->findChild<QPushButton *>(QStringLiteral("countPrepare"))->click();
    auto table = m_page->findChild<QTableWidget *>(QStringLiteral("countTable"));
    QCOMPARE(table->rowCount(), 3);

    auto entry = m_page->findChild<QLineEdit *>(QStringLiteral("countEntry"));
    for (int i = 0; i < 3; ++i) {               // three scans of the same article
        QTest::keyClicks(entry, QStringLiteral("7840001"));
        QTest::keyClick(entry, Qt::Key_Return);
    }

    int row = -1;
    for (int i = 0; i < table->rowCount(); ++i) {
        if (table->item(i, 0)->text() == QLatin1String("7840001"))
            row = i;
    }
    QVERIFY(row >= 0);
    QCOMPARE(table->item(row, 3)->text(), QStringLiteral("3"));
    QCOMPARE(table->item(row, 4)->text(), QStringLiteral("-2"));

    // QMessageBox::showEvent asks for the native window, which Qt's offscreen platform doesn't
    // have on Windows: it crashes there. The service side is covered in tst_inventory.
    if (QGuiApplication::platformName() == QLatin1String("offscreen"))
        QSKIP("A QMessageBox can't be shown on the offscreen platform");

    whenDialogOpens([](QDialog *question) {
        for (QAbstractButton *b : question->findChildren<QAbstractButton *>()) {
            if (qobject_cast<QMessageBox *>(question)->buttonRole(b) == QMessageBox::YesRole)
                b->click();
        }
    });
    m_page->findChild<QPushButton *>(QStringLiteral("countApply"))->click();

    QCOMPARE(m_core.products->product(QStringLiteral("7840001"))->stock, AguaraQuantity::whole(3));
    QCOMPARE(m_core.products->product(QStringLiteral("7840002"))->stock, Quantity(0));     // not counted
}

void tst_Modules::adjustThroughTheScreen()
{
    QLineEdit *search = nullptr;
    QTableView *table = showInventory(&search);
    QVERIFY(table);
    table->selectRow(0);

    bool done = false;
    whenDialogOpens([&](QDialog *dialog) {
        dialog->findChild<QLineEdit *>(QStringLiteral("adjustQuantity"))->setText(QStringLiteral("2"));
        dialog->accept();
        done = true;
    });
    QMenu *menu = m_page->findChild<QPushButton *>(QStringLiteral("moreButton"))->menu();
    QVERIFY(menu);
    menu->actions().first()->trigger();         // Ajustar stock...
    QVERIFY(done);

    const QString code = table->model()->index(0, int(ProductColumn::Code)).data().toString();
    QCOMPARE(m_core.products->product(code)->stock, -AguaraQuantity::whole(2));    // a way out, by default
    const StockMovement move = m_core.inventory->movements(code).first();
    QCOMPARE(move.type, QLatin1String(StockMovementType::Adjustment));
    QCOMPARE(move.reason, QStringLiteral("Rotura"));
}

void tst_Modules::labelsFillPages()
{
    QList<Product> labels;
    Product p;
    p.code = QStringLiteral("4006381333931");
    p.description = QStringLiteral("YERBA MATE KURUPI MENTA 500G");
    p.price = 12500;
    for (int i = 0; i < LabelSheet::Columns * LabelSheet::Rows + 1; ++i)
        labels << p;

    const QString file = m_tmp.filePath(QStringLiteral("labels.pdf"));
    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(file);
    printer.setPageSize(QPageSize(QPageSize::A4));
    QCOMPARE(LabelSheet::print(&printer, labels), 2);       // one label over: a second page
    QVERIFY(QFileInfo(file).size() > 1000);
}

QTableView *tst_Modules::showInventory(QLineEdit **search)
{
    auto widgets = qobject_cast<WidgetModuleInterface *>(load(QStringLiteral("modulo_control_inventario.dll")));
    if (!widgets)
        return nullptr;
    const QList<Product> catalog = {
        [] { Product p; p.code = "7840001"; p.description = "YERBA KURUPI 500G"; p.price = 12000; return p; }(),
        [] { Product p; p.code = "7840002"; p.description = "GALLETITAS SURTIDAS"; p.price = 5000; return p; }(),
        [] { Product p; p.code = "7840003"; p.description = "ACEITE 900ML"; p.price = 14000; return p; }(),
    };
    if (!m_core.products->importProducts(catalog))
        return nullptr;

    m_page = widgets->createWidget();
    if (auto ag = qobject_cast<AguaraWidget *>(m_page))
        connect(ag, &AguaraWidget::notify, this, [this](const ModuleMsg &m) { m_lastNote = m.title + QStringLiteral(": ") + m.message; });
    m_page->show();
    QApplication::setActiveWindow(m_page);      // shortcuts only reach the active window
    if (!QTest::qWaitForWindowActive(m_page))
        return nullptr;
    *search = m_page->findChild<QLineEdit *>(QStringLiteral("itemSearch"));
    return m_page->findChild<QTableView *>(QStringLiteral("tableView"));
}

void tst_Modules::inventoryScannerSelectsCode()
{
    QLineEdit *search = nullptr;
    QTableView *table = showInventory(&search);
    QVERIFY(table && search);

    // a scanner types the code and presses Enter
    QTest::keyClick(m_page, Qt::Key_F, Qt::ControlModifier);
    QVERIFY(search->hasFocus());
    QTest::keyClicks(search, QStringLiteral("7840003"));
    QTest::keyClick(search, Qt::Key_Return);
    QVERIFY(table->selectionModel()->hasSelection());
    QCOMPARE(table->model()->index(table->currentIndex().row(), int(ProductColumn::Code)).data().toString(),
             QStringLiteral("7840003"));
    QCOMPARE(search->selectedText(), QStringLiteral("7840003"));    // next scan replaces it

    QTest::keyClicks(search, QStringLiteral("7840001"));
    QTest::keyClick(search, Qt::Key_Return);
    QCOMPARE(table->model()->index(table->currentIndex().row(), int(ProductColumn::Code)).data().toString(),
             QStringLiteral("7840001"));
}

void tst_Modules::inventorySearchByDescription()
{
    QLineEdit *search = nullptr;
    QTableView *table = showInventory(&search);
    QVERIFY(table && search);
    search->setFocus();
    QTest::keyClicks(search, QStringLiteral("galletitas"));
    QTest::keyClick(search, Qt::Key_Return);
    QCOMPARE(table->model()->index(table->currentIndex().row(), int(ProductColumn::Description)).data().toString(),
             QStringLiteral("GALLETITAS SURTIDAS"));
}

void tst_Modules::inventoryNotFoundNotifies()
{
    QLineEdit *search = nullptr;
    QTableView *table = showInventory(&search);
    QVERIFY(table && search);
    search->setFocus();
    QTest::keyClicks(search, QStringLiteral("0000000"));
    QTest::keyClick(search, Qt::Key_Return);
    QVERIFY2(m_lastNote.startsWith(QStringLiteral("Sin resultados")), qPrintable(m_lastNote));
}

QTEST_MAIN(tst_Modules)
#include "tst_modules.moc"
