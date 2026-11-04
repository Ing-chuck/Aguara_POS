// The real module plugins (built into <build>/bin/modules), driven through
// the same host and services the application gives them, on SQLite.
#include "testsupport.h"

#include "labelsheet.h"
#include "passwordhash.h"
#include "sqluserservice.h"

#include <interfaces.h>

#include <QDialog>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QPluginLoader>
#include <QAbstractButton>
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

private:
    void openUsers();
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
