// The real module plugins (built into <build>/bin/modules), driven through
// the same host and services the application gives them, on SQLite.
#include "testsupport.h"

#include "closingsheet.h"
#include "labelsheet.h"
#include "passwordhash.h"
#include "sqluserservice.h"

#include <interfaces.h>

#include <QCheckBox>
#include <QDateEdit>
#include <QDialog>
#include <QDoubleSpinBox>
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
#include <barcode.h>
#include <formcheck.h>
#include "telemetry.h"
#include <livesearch.h>
#include <QPrinter>
#include <QPushButton>
#include <QRadioButton>
#include <QTableView>
#include <QTimer>
#include <QToolButton>
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
    void importCustomers();
    void importCustomersTwiceChangesNothing();
    void importWrongFileSavesNothing();
    void importSuppliers();
    void importTheUseByDate();
    void importShowsOnTheOpenPage();

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
    void expiredArticleAsksAtTheTill();
    void discardingAtTheTillLeavesTheSale();
    void discardingOnlySomeLeavesTheRestInTheSale();
    void discardingNeedsASupervisor();
    void aPromotionIsMadeOnItsPage();
    void aScannedCodeDoesntSaveThePromotion();
    void pickingManyArticlesAtOnce();
    void aPromotionComesBackAsItWas();
    void aPromotionReachesTheTill();
    void aPromotionFollowsTheQuantity();
    void aPromotionDoesntUseTheCashiersLimit();
    void aHeldSaleGetsTodaysPromotions();
    void anOutageKeepsThePromotionsKnown();
    void aPromotionReachesTheTicket();
    void goodArticlesDontAsk();
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
    void inventoryNarrowsAsYouType();
    void articleSearchFindsCodesToo();
    void tillSuggestsArticles();
    void scanningNeverOpensTheSuggestions();
    void suppliersNarrowAsYouType();
    void altaDeProductos();
    void altaCreatesTheArticleOnTheSpot();
    void altaCreatesTheSupplierOnTheSpot();
    void telemetrySeesWhatHappensNotAboutWhom();
    void emptyEnterDoesNotCharge();
    void scannerCantPayInCash();
    void cashCantBeAbsurd();
    void removalAsksAndEnterKeeps();
    void removingARoleAsks();
    void deactivatingAUserAsks();
    void articleFormSaysWhatIsMissing();
    void articleFormManagesUnitsAndTheUseByDate();
    void articleFormKeepsARemovedUnit();
    void lotsThroughTheHistory();
    void countRefusesWhatItCantRead();
    void purchaseChecksItsLines();
    void priceChangeNeedsSomethingToChange();
    void cashMovementSaysWhatIsMissing();
    void removingAVoucherAsks();
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
    void supervisorSaysYes();
    QWidget *showPromotions();
    qint64 percentOff(const QString &code, Money percent, Quantity minimum = 0);
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
    auto importer = qobject_cast<ImportInterface *>(load(QStringLiteral("modulo_importar.dll")));
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
    QVERIFY(importer->importFrom(csv, ImportKind::Articles));
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
    auto importer = qobject_cast<ImportInterface *>(load(QStringLiteral("modulo_importar.dll")));
    QVERIFY(importer);
    const QString csv = writeFile(QStringLiteral("quoted.csv"),
        "Codigo,Descripcion,Marca,Precio\r\n"
        "101,\"GALLETITAS, SURTIDAS\",\"ARCOR\",\"12,500\"\r\n"
        "102,\"VINO \"\"TINTO\"\" 750ML\",,25000\r\n"
        "103,\"CAJA\nGRANDE\",,1000\r\n");
    QVERIFY(importer->importFrom(csv, ImportKind::Articles));
    QCOMPARE(m_core.products->product(QStringLiteral("101"))->description, QStringLiteral("GALLETITAS, SURTIDAS"));
    QCOMPARE(m_core.products->product(QStringLiteral("101"))->price, Money(12500));
    QCOMPARE(m_core.products->product(QStringLiteral("101"))->brandName, QStringLiteral("ARCOR"));
    QCOMPARE(m_core.products->product(QStringLiteral("102"))->description, QStringLiteral("VINO \"TINTO\" 750ML"));
    QCOMPARE(m_core.products->product(QStringLiteral("103"))->description, QStringLiteral("CAJA\nGRANDE"));
}

void tst_Modules::importExcelWindows1252()
{
    auto importer = qobject_cast<ImportInterface *>(load(QStringLiteral("modulo_importar.dll")));
    QVERIFY(importer);
    // what Excel writes on Spanish Windows ("CSV (delimitado por comas)")
    const QString csv = writeFile(QStringLiteral("excel.csv"),
        "C\xF3" "digo;Descripci\xF3" "n;Precio Venta\r\n201;CA\xD1" "A DE AZ\xDA" "CAR;8.000\r\n");
    QVERIFY(importer->importFrom(csv, ImportKind::Articles));
    const auto p = m_core.products->product(QStringLiteral("201"));
    QVERIFY(p.has_value());
    QCOMPARE(p->description, QString::fromUtf8("CAÑA DE AZÚCAR"));
    QCOMPARE(p->price, Money(8000));
}

void tst_Modules::importCustomers()
{
    auto importer = qobject_cast<ImportInterface *>(load(QStringLiteral("modulo_importar.dll")));
    QVERIFY(importer);
    QVERIFY(importer->canImport(ImportKind::Customers));

    Customer ande;
    ande.name = QStringLiteral("ANDE");
    ande.documentType = QString::fromLatin1(DocumentType::Ruc);
    ande.document = QStringLiteral("80009735-1");
    ande.address = QStringLiteral("Asuncion");
    ande.phone = QStringLiteral("021 555 000");
    const qint64 andeId = m_core.customers->saveCustomer(ande);
    Customer maria;
    maria.name = QStringLiteral("Maria Lopez");
    maria.phone = QStringLiteral("0981 000 111");
    const qint64 mariaId = m_core.customers->saveCustomer(maria);
    Customer juan;
    juan.name = QStringLiteral("Juan Perez");
    QVERIFY(m_core.customers->saveCustomer(juan));
    QVERIFY(m_core.customers->saveCustomer(juan));
    Customer gone;
    gone.name = QStringLiteral("Se Fue");
    gone.documentType = QString::fromLatin1(DocumentType::Ci);
    gone.document = QStringLiteral("999999");
    const qint64 goneId = m_core.customers->saveCustomer(gone);
    QVERIFY(m_core.customers->setCustomerActive(goneId, false));

    const QString csv = writeFile(QStringLiteral("clientes.csv"), QString::fromUtf8(
        "Lista de clientes\n"
        "Nombre;RUC;Cédula;Dirección;Teléfono;Correo;Saldo\n"
        "A.N.D.E.;80.009.735-1;;;0981 111 222;;500\n"
        "maria  lopez;;;Luque;;maria@mail.com;\n"
        "Nuevo Uno;;1.234.567;;;;\n"
        "Juan Perez;;;;0981 222 333;;\n"
        "Mal RUC;80009735-9;;;;;\n"
        ";;;Sin nombre;;;\n"
        "Mal Correo;;;;;no-es-correo;\n"
        "Nuevo Otra Vez;;1234567;;;a@b.com;\n"
        "Se Fue Todavia;;999999;;;;\n").toUtf8());
    QVERIFY2(importer->importFrom(csv, ImportKind::Customers), qPrintable(m_lastNote));
    QVERIFY2(m_lastNote.contains(QStringLiteral("4 clientes importados")), qPrintable(m_lastNote));
    QVERIFY2(m_lastNote.contains(QStringLiteral("nuevos: 1, actualizados: 3")), qPrintable(m_lastNote));
    QVERIFY2(m_lastNote.contains(QString::fromUtf8("4 líneas omitidas")), qPrintable(m_lastNote));
    QVERIFY2(m_lastNote.contains(QStringLiteral("varios clientes llamados Juan Perez")), qPrintable(m_lastNote));
    QVERIFY2(m_lastNote.contains(QStringLiteral("80009735-9")), qPrintable(m_lastNote));
    QVERIFY2(m_lastNote.contains(QStringLiteral("siguen")), qPrintable(m_lastNote));

    const auto updated = m_core.customers->customer(andeId);
    QCOMPARE(updated->name, QStringLiteral("A.N.D.E."));
    QCOMPARE(updated->phone, QStringLiteral("0981 111 222"));
    QCOMPARE(updated->address, QStringLiteral("Asuncion"));         // left empty: kept

    const auto byName = m_core.customers->customer(mariaId);
    QCOMPARE(byName->name, QStringLiteral("maria lopez"));
    QCOMPARE(byName->address, QStringLiteral("Luque"));
    QCOMPARE(byName->phone, QStringLiteral("0981 000 111"));
    QVERIFY(byName->document.isEmpty());

    const QList<Customer> repeated = m_core.customers->customers(QStringLiteral("1234567"));
    QCOMPARE(repeated.size(), 1);                                   // two lines, one customer
    QCOMPARE(repeated.first().name, QStringLiteral("Nuevo Otra Vez"));
    QCOMPARE(repeated.first().email, QStringLiteral("a@b.com"));
    QCOMPARE(repeated.first().documentType, QString::fromLatin1(DocumentType::Ci));

    const auto stillGone = m_core.customers->customer(goneId);
    QCOMPARE(stillGone->name, QStringLiteral("Se Fue Todavia"));
    QVERIFY(!stillGone->active);
}

void tst_Modules::importCustomersTwiceChangesNothing()
{
    auto importer = qobject_cast<ImportInterface *>(load(QStringLiteral("modulo_importar.dll")));
    QVERIFY(importer);
    const QString csv = writeFile(QStringLiteral("clientes.csv"),
        "Cliente,Documento,Tipo,Celular\n"
        "ANDE,80009735-1,,\n"
        "Pedro Gomez,4567890,CI,0981 333 444\n"
        "Sin Documento,,,0981 555 666\n"
        "Otra Cosa,123,Pasaporte,\n");
    QVERIFY2(importer->importFrom(csv, ImportKind::Customers), qPrintable(m_lastNote));
    QVERIFY2(m_lastNote.contains(QStringLiteral("nuevos: 3, actualizados: 0")), qPrintable(m_lastNote));
    QVERIFY2(m_lastNote.contains(QStringLiteral("Pasaporte")), qPrintable(m_lastNote));
    QVERIFY2(importer->importFrom(csv, ImportKind::Customers), qPrintable(m_lastNote));
    QVERIFY2(m_lastNote.contains(QStringLiteral("nuevos: 0, actualizados: 3")), qPrintable(m_lastNote));
    QCOMPARE(m_core.customers->customers().size(), 3);
    QCOMPARE(m_core.customers->customers(QStringLiteral("ANDE")).first().documentType,
             QString::fromLatin1(DocumentType::Ruc));
}

void tst_Modules::importWrongFileSavesNothing()
{
    auto importer = qobject_cast<ImportInterface *>(load(QStringLiteral("modulo_importar.dll")));
    QVERIFY(importer);
    const QString articles = writeFile(QStringLiteral("articulos.csv"),
        "Codigo,Descripcion,Precio\n"
        "777,GASEOSA,15000\n");
    QVERIFY(!importer->importFrom(articles, ImportKind::Customers));
    QVERIFY2(m_lastNote.contains(QStringLiteral("columna Nombre")), qPrintable(m_lastNote));
    QVERIFY(m_core.customers->customers().isEmpty());
    QVERIFY(!importer->importFrom(articles, ImportKind::Suppliers));
    QVERIFY(m_core.inventory->suppliers().isEmpty());
}

void tst_Modules::importSuppliers()
{
    auto importer = qobject_cast<ImportInterface *>(load(QStringLiteral("modulo_importar.dll")));
    QVERIFY(importer);
    QVERIFY(importer->canImport(ImportKind::Suppliers));

    Supplier sur;
    sur.name = QStringLiteral("Distribuidora Sur");
    sur.phone = QStringLiteral("021 111 222");
    sur.contact = QStringLiteral("Ana");
    QVERIFY(m_core.inventory->saveSupplier(sur));

    const QString csv = writeFile(QStringLiteral("proveedores.csv"), QString::fromUtf8(
        "Proveedor,Teléfono,Correo,Contacto,Web,Observaciones,RUC\n"
        "DISTRIBUIDORA  SUR,,ventas@sur.com.py,,,Paga a 30 dias,80000000-1\n"
        "Frigorifico Norte,0981 777 888,,Luis,www.norte.com.py,,\n"
        "frigorifico norte,,info@norte.com.py,,,,\n"
        ",0981 777 999,,,,,\n"
        "Mal Telefono,sin numero,,,,,\n"
        "Mal Web,,,,no es web,,\n").toUtf8());
    QVERIFY(importer->importFrom(csv, ImportKind::Suppliers));
    QVERIFY2(m_lastNote.contains(QStringLiteral("2 proveedores importados")), qPrintable(m_lastNote));
    QVERIFY2(m_lastNote.contains(QStringLiteral("nuevos: 1, actualizados: 1")), qPrintable(m_lastNote));
    QVERIFY2(m_lastNote.contains(QString::fromUtf8("3 líneas omitidas")), qPrintable(m_lastNote));

    const QList<Supplier> list = m_core.inventory->suppliers();
    QCOMPARE(list.size(), 2);
    const Supplier first = list.at(0);
    QCOMPARE(first.name, QStringLiteral("DISTRIBUIDORA SUR"));
    QCOMPARE(first.phone, QStringLiteral("021 111 222"));           // left empty: kept
    QCOMPARE(first.contact, QStringLiteral("Ana"));
    QCOMPARE(first.email, QStringLiteral("ventas@sur.com.py"));
    QCOMPARE(first.notes, QStringLiteral("Paga a 30 dias"));
    const Supplier second = list.at(1);
    QCOMPARE(second.phone, QStringLiteral("0981 777 888"));
    QCOMPARE(second.email, QStringLiteral("info@norte.com.py"));
    QCOMPARE(second.web, QStringLiteral("www.norte.com.py"));
}

void tst_Modules::importTheUseByDate()
{
    auto importer = qobject_cast<ImportInterface *>(load(QStringLiteral("modulo_importar.dll")));
    QVERIFY(importer);
    QVERIFY(importer->importFrom(writeFile(QStringLiteral("a.csv"),
        "Codigo;Descripcion;Precio;Stock;Fecha Vencimiento\n"
        "1;LECHE;9000;10;15/03/2031\n"
        "2;YERBA;9000;10;\n"
        "3;QUESO;9000;10;mañana\n"), ImportKind::Articles));
    QVERIFY2(m_lastNote.contains(QString::fromUtf8("línea 4")), qPrintable(m_lastNote));
    QCOMPARE(m_core.products->product(QStringLiteral("1"))->expires, QDate(2031, 3, 15));
    QVERIFY(!m_core.products->product(QStringLiteral("2"))->expires.isValid());

    // a price list without the column leaves it as it was
    QVERIFY(importer->importFrom(writeFile(QStringLiteral("b.csv"),
        "Codigo;Precio\n1;9500\n"), ImportKind::Articles));
    QCOMPARE(m_core.products->product(QStringLiteral("1"))->price, Money(9500));
    QCOMPARE(m_core.products->product(QStringLiteral("1"))->expires, QDate(2031, 3, 15));

    // a date for an article that exists goes to its stock with none, once
    const QString dates = writeFile(QStringLiteral("c.csv"), "Codigo;Vencimiento\n2;01/02/2032\n");
    QVERIFY(importer->importFrom(dates, ImportKind::Articles));
    QCOMPARE(m_core.inventory->lots(QStringLiteral("2")).first().quantity, AguaraQuantity::whole(10));
    QVERIFY(importer->importFrom(dates, ImportKind::Articles));
    QCOMPARE(m_core.inventory->lots(QStringLiteral("2")).size(), 1);
    QCOMPARE(m_core.inventory->lots(QStringLiteral("2")).first().quantity, AguaraQuantity::whole(10));
}

void tst_Modules::importShowsOnTheOpenPage()
{
    m_users.reset(new SqlUserService(m_core.monitor.get()));
    m_core.host->registerService(UserService_iid, m_users.get());
    auto factory = qobject_cast<WidgetModuleInterface *>(load(QStringLiteral("modulo_customers.dll")));
    QVERIFY(factory);
    m_page = factory->createWidget();
    auto table = m_page->findChild<QTableWidget *>(QStringLiteral("customerTable"));
    QVERIFY(table);
    QCOMPARE(table->rowCount(), 0);

    auto importer = qobject_cast<ImportInterface *>(load(QStringLiteral("modulo_importar.dll")));
    QVERIFY(importer);
    QVERIFY(importer->importFrom(writeFile(QStringLiteral("c.csv"), "Nombre\nPedro\nAna\n"), ImportKind::Customers));
    QCOMPARE(table->rowCount(), 2);
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

/// A person's pace: at full speed the keys look like a scanner.
static void typeByHand(QWidget *field, const QString &text)
{
    QTest::keyClicks(field, text, Qt::NoModifier, 60);
}

/// Exact cash takes two Enters; anything else, one.
static void enterUntilCharged(QDialog *payment)
{
    payment->accept();
    if (payment->isVisible())
        payment->accept();
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
        typeInCell(QStringLiteral("35"), Qt::Key_Tab);          // Margen: 4.000 / 0,65, up to the next 100
        QCOMPARE(lines->item(0, 5)->text(), QStringLiteral("6.200"));
        typeInCell(QStringLiteral("6.500"), Qt::Key_unknown);   // Precio, still being typed when saving
        QCOMPARE(lines->item(0, 7)->text(), QStringLiteral("48.000"));
        QVERIFY(dialog->findChild<QLabel *>(QStringLiteral("purchaseTotal"))->text().contains(QStringLiteral("48.000")));
        // the use-by date is optional, but it has to be one, and still to come
        auto refused = dialog->findChild<QLabel *>(QStringLiteral("purchaseError"));
        lines->item(0, 6)->setText(QStringLiteral("31/02/2031"));
        dialog->accept();
        QVERIFY(dialog->isVisible());
        QVERIFY2(refused->text().contains(QStringLiteral("dd/mm/aaaa")), qPrintable(refused->text()));
        lines->item(0, 6)->setText(QStringLiteral("01/01/2020"));
        dialog->accept();
        QVERIFY(dialog->isVisible());
        lines->item(0, 6)->setText(QStringLiteral("1/6/2031"));
        const QString code = lines->item(0, 0)->text();

        done = true;
        whenDialogOpens([](QDialog *labels) { labels->reject(); });
        dialog->accept();
        QCOMPARE(m_core.inventory->lots(code).size(), 1);
        QCOMPARE(m_core.inventory->lots(code).first().expires, QDate(2031, 6, 1));
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

        scan();                                                 // as in a sale: one more on its line
        QVERIFY(dialog->isVisible());
        QCOMPARE(lines->rowCount(), 1);
        QCOMPARE(lines->item(0, 2)->text(), QStringLiteral("4"));
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
        typeByHand(tendered, QStringLiteral("20.000"));
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

void tst_Modules::expiredArticleAsksAtTheTill()
{
    QVERIFY(showSales());
    Product p = *m_core.products->product(QStringLiteral("7840001"));
    p.stock = AguaraQuantity::whole(10);
    p.stockExpires = QDate::currentDate().addDays(-2);
    QVERIFY(m_core.products->saveProduct(p));
    QVERIFY(openTill(1));

    bool asked = false;
    whenDialogOpens([&](QDialog *alert) {
        asked = alert->objectName() == QLatin1String("expiryAlert");
        QVERIFY(asked);
        QVERIFY2(alert->findChild<QLabel *>(QStringLiteral("expiryText"))->text().contains(QStringLiteral("verificar la fecha")),
                 "the message");
        // a scanner's Enter, or an Esc, answers nothing
        QTest::keyClick(alert, Qt::Key_Return);
        QVERIFY(alert->isVisible());
        QTest::keyClick(alert, Qt::Key_Escape);
        QVERIFY(alert->isVisible());
        alert->findChild<QPushButton *>(QStringLiteral("expiryOk"))->click();
    });
    scan(QStringLiteral("7840001"));
    QVERIFY(asked);
    QCOMPARE(saleLines()->rowCount(), 1);
    QCOMPARE(m_core.products->product(p.code)->stock, AguaraQuantity::whole(10));
}

void tst_Modules::supervisorSaysYes()
{
    m_users->setSupervisorPrompt([](const QString &, const QString &, const QString &, QWidget *,
                                    QString *username, QString *password) {
        *username = QStringLiteral("encargado");
        *password = QStringLiteral("clave123");
        return true;
    });
}

void tst_Modules::discardingAtTheTillLeavesTheSale()
{
    QVERIFY(showSales());
    Product p = *m_core.products->product(QStringLiteral("7840001"));
    p.stock = AguaraQuantity::whole(10);
    p.stockExpires = QDate::currentDate().addDays(-2);
    QVERIFY(m_core.products->saveProduct(p));
    QVERIFY(openTill(1));
    supervisorSaysYes();

    whenDialogOpens([](QDialog *alert) {
        alert->findChild<QPushButton *>(QStringLiteral("expiryDiscard"))->click();
        QCOMPARE(alert->findChild<QLineEdit *>(QStringLiteral("expiryCount"))->text(), QStringLiteral("3"));
        alert->findChild<QPushButton *>(QStringLiteral("expiryConfirm"))->click();
    });
    scan(QStringLiteral("3*7840001"));
    QCOMPARE(saleLines()->rowCount(), 0);
    QCOMPARE(m_core.products->product(p.code)->stock, AguaraQuantity::whole(7));    // the quantity of the sale
    QCOMPARE(m_core.inventory->lots(p.code).first().quantity, AguaraQuantity::whole(7));
    const QList<StockMovement> moves = m_core.inventory->movements(p.code);
    QCOMPARE(moves.first().type, QString::fromLatin1(StockMovementType::Adjustment));
    QVERIFY(moves.first().reason.contains(QStringLiteral("Vencido")));
    QVERIFY2(moves.first().reason.contains(QStringLiteral("encargado")), qPrintable(moves.first().reason));
}

void tst_Modules::discardingOnlySomeLeavesTheRestInTheSale()
{
    QVERIFY(showSales());
    Product p = *m_core.products->product(QStringLiteral("7840001"));
    p.stock = AguaraQuantity::whole(2);
    p.stockExpires = QDate::currentDate().addDays(-2);
    QVERIFY(m_core.products->saveProduct(p));
    QVERIFY(m_core.inventory->adjustStock(p.code, AguaraQuantity::whole(10), QStringLiteral("encontrado")));
    QVERIFY(openTill(1));
    supervisorSaysYes();

    whenDialogOpens([](QDialog *alert) {
        auto count = alert->findChild<QLineEdit *>(QStringLiteral("expiryCount"));
        auto confirm = alert->findChild<QPushButton *>(QStringLiteral("expiryConfirm"));
        auto problem = alert->findChild<QLabel *>(QStringLiteral("expiryProblem"));

        // Volver is still a way out of it
        alert->findChild<QPushButton *>(QStringLiteral("expiryDiscard"))->click();
        alert->findChild<QPushButton *>(QStringLiteral("expiryBack"))->click();
        QVERIFY(alert->isVisible());
        QVERIFY(alert->findChild<QPushButton *>(QStringLiteral("expiryOk"))->isVisibleTo(alert));

        alert->findChild<QPushButton *>(QStringLiteral("expiryDiscard"))->click();
        QCOMPARE(count->text(), QStringLiteral("2"));       // what the lots say is expired, not the 6
        for (const char *wrong : { "", "0", "7", "abc", "1,5" }) {
            count->setText(QLatin1String(wrong));
            confirm->click();
            QVERIFY2(alert->isVisible(), wrong);
            QVERIFY2(!problem->isHidden(), wrong);
        }
        count->setText(QStringLiteral("7840001"));          // the scanner, read into the field
        QTest::keyClick(count, Qt::Key_Return);
        QVERIFY(alert->isVisible());

        count->setText(QStringLiteral("2"));
        QTest::keyClick(count, Qt::Key_Return);             // typed by hand, Enter does confirm
        QVERIFY(!alert->isVisible());
    });
    scan(QStringLiteral("6*7840001"));
    QCOMPARE(saleLines()->rowCount(), 1);
    QCOMPARE(saleLines()->item(0, 0)->text().isEmpty(), false);
    bool four = false;
    for (int column = 0; column < saleLines()->columnCount(); ++column)
        four = four || (saleLines()->item(0, column) && saleLines()->item(0, column)->text() == QLatin1String("4"));
    QVERIFY(four);                                          // the other 4 are sold
    QCOMPARE(m_core.products->product(p.code)->stock, AguaraQuantity::whole(10));
    QVERIFY(m_core.inventory->lots(p.code).isEmpty());      // the expired lot is gone
}

void tst_Modules::goodArticlesDontAsk()
{
    QVERIFY(showSales());
    Product p = *m_core.products->product(QStringLiteral("7840001"));
    p.stock = AguaraQuantity::whole(10);
    p.stockExpires = QDate::currentDate();      // the last day is still good
    QVERIFY(m_core.products->saveProduct(p));
    QVERIFY(openTill(1));
    scan(QStringLiteral("7840001"));            // a dialog here would never be answered
    QCOMPARE(saleLines()->rowCount(), 1);
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

    whenDialogOpens([](QDialog *ask) { ask->reject(); });
    press(Qt::Key_Delete);
    QCOMPARE(saleLines()->rowCount(), 1);               // they changed their mind

    whenDialogOpens([](QDialog *ask) { ask->accept(); });
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
        typeByHand(tendered, QStringLiteral("10.000"));
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
    whenDialogOpens([](QDialog *payment) { enterUntilCharged(payment); });
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
        typeByHand(tendered, QStringLiteral("10.000"));
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
        typeByHand(tendered, QStringLiteral("20.000"));
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
    whenDialogOpens([](QDialog *payment) { enterUntilCharged(payment); });
    press(Qt::Key_F12);
    QCOMPARE(m_core.sales->sessionSales(m_core.cash->currentSession()->id).first().customerName, QStringLiteral("DOÑA ROSA"));
    QCOMPARE(label("saleCustomer")->text(), QStringLiteral("Consumidor final"));    // the next sale starts clean
}

/// Up to the point of the sale; the cashier has charged 2 of the 7840001.
static void chargeTwo(QDialog *payment)
{
    enterUntilCharged(payment);
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
    whenDialogOpens([](QDialog *payment) { enterUntilCharged(payment); });
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
        enterUntilCharged(payment);
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
        enterUntilCharged(payment);
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

        const auto add = [&](const QString &name) {
            dialog->findChild<QLineEdit *>(QStringLiteral("methodsNew"))->setText(name);
            dialog->findChild<QPushButton *>(QStringLiteral("methodsAdd"))->click();
        };
        auto error = dialog->findChild<QLabel *>(QStringLiteral("methodsError"));
        add(QString());
        QVERIFY(!error->isHidden());
        add(QStringLiteral("CHEQUE"));                  // the same one, written differently
        add(QStringLiteral("Efectivo"));                // always there
        QCOMPARE(table->rowCount(), 5);

        whenDialogOpens([](QDialog *ask) { ask->reject(); });
        dialog->accept();
        QVERIFY(dialog->isVisible());                   // taking QR away has to be confirmed
        whenDialogOpens([](QDialog *ask) { ask->accept(); });
        dialog->accept();
    });
    trigger("actionPaymentMethods");

    QStringList codes;
    for (const PaymentMethodInfo &m : m_core.sales->paymentMethods())
        codes << m.code;
    QVERIFY(codes.contains(QStringLiteral("Cheque")));
    QVERIFY(!codes.contains(QStringLiteral("qr")));
}

void tst_Modules::discardingNeedsASupervisor()
{
    QVERIFY(showSales());
    Product p = *m_core.products->product(QStringLiteral("7840001"));
    p.stock = AguaraQuantity::whole(10);
    p.stockExpires = QDate::currentDate().addDays(-2);
    QVERIFY(m_core.products->saveProduct(p));
    QVERIFY(openTill(1));

    int asked = 0;
    m_users->setSupervisorPrompt([&](const QString &, const QString &, const QString &, QWidget *, QString *, QString *) {
        ++asked;
        return false;
    });
    const auto discard = [](QDialog *alert) {
        alert->findChild<QPushButton *>(QStringLiteral("expiryDiscard"))->click();
        alert->findChild<QPushButton *>(QStringLiteral("expiryConfirm"))->click();
    };
    // refused: the alert comes back, and the cashier still has to answer it
    whenDialogOpens([=](QDialog *alert) {
        discard(alert);
        whenDialogOpens([](QDialog *again) {
            again->findChild<QPushButton *>(QStringLiteral("expiryOk"))->click();
        });
    });
    scan(QStringLiteral("7840001"));
    QCOMPARE(asked, 1);
    QCOMPARE(m_core.products->product(p.code)->stock, AguaraQuantity::whole(10));
    QCOMPARE(m_core.inventory->movements(p.code).first().reason.contains(QStringLiteral("Vencido")), false);
    QCOMPARE(saleLines()->rowCount(), 1);               // OK: the unit was good
}

// --- promotions --------------------------------------------------------------

QWidget *tst_Modules::showPromotions()
{
    openUsers();
    auto factory = load(QStringLiteral("modulo_ventas_promociones.dll"));
    auto provider = qobject_cast<PermissionProvider *>(factory);
    if (!provider || !m_users->registerPermissions(provider->permissions()))
        return nullptr;
    if (!m_users->login(QStringLiteral("encargado"), QStringLiteral("clave123")))
        return nullptr;
    for (const char *code : { "1001", "1002", "1003" }) {
        Product p;
        p.code = QLatin1String(code);
        p.description = QStringLiteral("ARTICULO ") + p.code;
        p.price = 10000;
        p.categoryName = QStringLiteral("LACTEOS");
        p.brandName = QLatin1String(code) == QLatin1String("1003") ? QStringLiteral("OTRA") : QStringLiteral("LACTOLANDA");
        if (!m_core.products->saveProduct(p))
            return nullptr;
    }
    m_page = dynamic_cast<WidgetModuleInterface *>(factory)->createWidget();
    m_page->resize(1100, 700);
    m_page->show();
    return m_page;
}

qint64 tst_Modules::percentOff(const QString &code, Money percent, Quantity minimum)
{
    Promotion p;
    p.name = QStringLiteral("PROMO ") + code;
    p.value = percent;
    p.minimum = minimum;
    p.from = QDate::currentDate();
    p.to = p.from.addDays(3);
    PromotionRule rule;
    rule.code = code;
    p.rules << rule;
    return m_core.promotions->savePromotion(p);
}

void tst_Modules::aPromotionIsMadeOnItsPage()
{
    QVERIFY(showPromotions());
    whenDialogOpens([](QDialog *dialog) {
        auto reach = dialog->findChild<QLabel *>(QStringLiteral("promoReach"));
        auto save = dialog->findChild<QPushButton *>(QStringLiteral("promoSave"));
        auto error = dialog->findChild<QLabel *>(QStringLiteral("promoError"));
        QVERIFY(reach->text().contains(QStringLiteral("Sin artículos")));

        save->click();                                  // no name: it says so and stays
        QVERIFY(dialog->isVisible());
        QVERIFY(error->text().contains(QStringLiteral("nombre")));
        dialog->findChild<QLineEdit *>(QStringLiteral("promoName"))->setText(QStringLiteral("SEMANA DEL LACTEO"));
        dialog->findChild<QLineEdit *>(QStringLiteral("promoValue"))->setText(QStringLiteral("20"));

        save->click();                                  // nothing chosen
        QVERIFY(dialog->isVisible());
        QVERIFY(error->text().contains(QStringLiteral("artículos")));

        // the whole brand, less one article
        dialog->findChild<QComboBox *>(QStringLiteral("promoTarget"))->setCurrentIndex(PromotionRule::Brand);
        auto brand = dialog->findChild<QComboBox *>(QStringLiteral("promoBrand"));
        brand->setCurrentIndex(brand->findText(QStringLiteral("LACTOLANDA")));
        dialog->findChild<QPushButton *>(QStringLiteral("promoInclude"))->click();
        QCOMPARE(reach->text(), QStringLiteral("Alcanza a 2 artículos"));

        dialog->findChild<QComboBox *>(QStringLiteral("promoTarget"))->setCurrentIndex(PromotionRule::Article);
        dialog->findChild<QLineEdit *>(QStringLiteral("promoArticle"))->setText(QStringLiteral("1002"));
        dialog->findChild<QPushButton *>(QStringLiteral("promoExclude"))->click();
        QCOMPARE(reach->text(), QStringLiteral("Alcanza a 1 artículo"));

        auto invert = dialog->findChild<QCheckBox *>(QStringLiteral("promoInverted"));
        invert->setChecked(true);                       // 1003 only: 1002 is still left out
        QCOMPARE(reach->text(), QStringLiteral("Alcanza a 1 artículo"));
        invert->setChecked(false);

        auto rules = dialog->findChild<QTableWidget *>(QStringLiteral("promoRules"));
        QCOMPARE(rules->rowCount(), 2);
        QCOMPARE(rules->item(1, 2)->text(), QStringLiteral("Excluye"));
        rules->selectRow(1);
        dialog->findChild<QPushButton *>(QStringLiteral("promoRuleRemove"))->click();
        QCOMPARE(rules->rowCount(), 1);
        QCOMPARE(reach->text(), QStringLiteral("Alcanza a 2 artículos"));

        dialog->findChild<QCheckBox *>(QStringLiteral("promoDay6"))->setChecked(false);
        save->click();
        QVERIFY(!dialog->isVisible());
    });
    m_page->findChild<QPushButton *>(QStringLiteral("promoAdd"))->click();

    auto table = m_page->findChild<QTableWidget *>(QStringLiteral("promoTable"));
    QCOMPARE(table->rowCount(), 1);
    QCOMPARE(table->item(0, 1)->text(), QStringLiteral("20 %"));
    const Promotion saved = m_core.promotions->promotions().first();
    QCOMPARE(saved.weekdays, 63);
    QCOMPARE(saved.rules.size(), 1);

    whenDialogOpens([](QDialog *list) {
        QCOMPARE(list->findChild<QTableWidget *>(QStringLiteral("promoArticles"))->rowCount(), 2);
        QCOMPARE(list->findChild<QTableWidget *>(QStringLiteral("promoArticles"))->item(0, 5)->text(), QStringLiteral("8.000"));
    });
    m_page->findChild<QPushButton *>(QStringLiteral("promoShow"))->click();

    whenDialogOpens([](QDialog *ask) { ask->accept(); });
    m_page->findChild<QPushButton *>(QStringLiteral("promoToggle"))->click();
    QCOMPARE(table->rowCount(), 0);
    m_page->findChild<QCheckBox *>(QStringLiteral("promoInactive"))->setChecked(true);
    QCOMPARE(table->rowCount(), 1);
    QCOMPARE(table->item(0, 5)->text(), QStringLiteral("De baja"));
}

void tst_Modules::aScannedCodeDoesntSaveThePromotion()
{
    QVERIFY(showPromotions());
    QVERIFY(m_core.promotions->promotions().isEmpty());
    whenDialogOpens([](QDialog *dialog) {
        dialog->findChild<QLineEdit *>(QStringLiteral("promoName"))->setText(QStringLiteral("X"));
        dialog->findChild<QLineEdit *>(QStringLiteral("promoValue"))->setText(QStringLiteral("10"));
        auto article = dialog->findChild<QLineEdit *>(QStringLiteral("promoArticle"));
        article->setFocus();
        QTest::keyClicks(article, QStringLiteral("1001"));
        QTest::keyClick(article, Qt::Key_Return);
        QTest::keyClicks(article, QStringLiteral("999"));
        QTest::keyClick(article, Qt::Key_Return);
        QVERIFY(dialog->isVisible());
        QCOMPARE(dialog->findChild<QTableWidget *>(QStringLiteral("promoRules"))->rowCount(), 1);
        QVERIFY(dialog->findChild<QLabel *>(QStringLiteral("promoError"))->text().contains(QStringLiteral("999")));
    });
    m_page->findChild<QPushButton *>(QStringLiteral("promoAdd"))->click();
    QVERIFY(m_core.promotions->promotions().isEmpty());
}

void tst_Modules::pickingManyArticlesAtOnce()
{
    QVERIFY(showPromotions());
    whenDialogOpens([](QDialog *dialog) {
        whenDialogOpens([](QDialog *pick) {
            auto list = pick->findChild<QTableView *>(QStringLiteral("pickList"));
            QCOMPARE(list->model()->rowCount(), 3);
            list->selectAll();
            pick->findChild<QPushButton *>(QStringLiteral("pickExclude"))->click();
        });
        dialog->findChild<QPushButton *>(QStringLiteral("promoPick"))->click();
        auto rules = dialog->findChild<QTableWidget *>(QStringLiteral("promoRules"));
        QCOMPARE(rules->rowCount(), 3);
        QCOMPARE(rules->item(0, 2)->text(), QStringLiteral("Excluye"));
        QCOMPARE(dialog->findChild<QLabel *>(QStringLiteral("promoReach"))->text(), QStringLiteral("Sin artículos elegidos"));
        dialog->findChild<QRadioButton *>(QStringLiteral("promoEverything"))->setChecked(true);
        QCOMPARE(dialog->findChild<QLabel *>(QStringLiteral("promoReach"))->text(), QStringLiteral("Alcanza a 0 artículos"));
    });
    m_page->findChild<QPushButton *>(QStringLiteral("promoAdd"))->click();
}

void tst_Modules::aPromotionComesBackAsItWas()
{
    QVERIFY(showPromotions());
    Promotion p;
    p.name = QStringLiteral("TRES POR DOS");
    p.kind = PromotionKind::BuyPay;
    p.buy = 3;
    p.pay = 2;
    p.from = QDate::currentDate().addDays(5);
    p.to = QDate::currentDate().addDays(9);
    p.weekdays = 3;
    p.everything = true;
    PromotionRule out;
    out.code = QStringLiteral("1001");
    out.excluded = true;
    p.rules << out;
    QVERIFY(m_core.promotions->savePromotion(p));
    QTest::qWait(0);
    m_page->hide();
    m_page->show();

    auto table = m_page->findChild<QTableWidget *>(QStringLiteral("promoTable"));
    QCOMPARE(table->item(0, 5)->text(), QStringLiteral("Programada"));
    QCOMPARE(table->item(0, 4)->text(), QStringLiteral("Lun, Mar"));

    bool opened = false;
    whenDialogOpens([&](QDialog *dialog) {
        opened = true;
        QCOMPARE(dialog->findChild<QDateEdit *>(QStringLiteral("promoFrom"))->date(), p.from);
        QCOMPARE(dialog->findChild<QDateEdit *>(QStringLiteral("promoTo"))->date(), p.to);
        QCOMPARE(dialog->findChild<QComboBox *>(QStringLiteral("promoKind"))->currentIndex(), int(PromotionKind::BuyPay));
        QCOMPARE(dialog->findChild<QSpinBox *>(QStringLiteral("promoBuy"))->value(), 3);
        QCOMPARE(dialog->findChild<QSpinBox *>(QStringLiteral("promoPay"))->value(), 2);
        QVERIFY(dialog->findChild<QRadioButton *>(QStringLiteral("promoEverything"))->isChecked());
        QVERIFY(dialog->findChild<QCheckBox *>(QStringLiteral("promoDay1"))->isChecked());
        QVERIFY(!dialog->findChild<QCheckBox *>(QStringLiteral("promoDay2"))->isChecked());
        QCOMPARE(dialog->findChild<QLabel *>(QStringLiteral("promoReach"))->text(), QStringLiteral("Alcanza a 2 artículos"));
        dialog->findChild<QPushButton *>(QStringLiteral("promoSave"))->click();
    });
    m_page->findChild<QPushButton *>(QStringLiteral("promoEdit"))->click();
    QVERIFY(opened);
    QCOMPARE(m_core.promotions->promotions().size(), 1);

    whenDialogOpens([](QDialog *dialog) {
        QCOMPARE(dialog->findChild<QLineEdit *>(QStringLiteral("promoName"))->text(), QStringLiteral("TRES POR DOS (copia)"));
        dialog->findChild<QPushButton *>(QStringLiteral("promoSave"))->click();
    });
    m_page->findChild<QPushButton *>(QStringLiteral("promoCopy"))->click();
    QCOMPARE(m_core.promotions->promotions().size(), 2);
}

void tst_Modules::aPromotionReachesTheTill()
{
    QVERIFY(showSales());
    QVERIFY(percentOff(QStringLiteral("7840001"), 10));
    QVERIFY(openTill());

    scan(QStringLiteral("7840001"));                    // 11.000
    QCOMPARE(saleLines()->item(0, 4)->text(), QStringLiteral("1.100"));
    QCOMPARE(saleLines()->item(0, 5)->text(), QStringLiteral("9.900"));
    QVERIFY2(label("saleMessage")->text().contains(QStringLiteral("PROMO 7840001")), qPrintable(label("saleMessage")->text()));
    QCOMPARE(label("saleTotal")->text(), QStringLiteral("9.900"));

    scan(QStringLiteral("7840001"));                    // one more on the same line
    QCOMPARE(saleLines()->rowCount(), 1);
    QCOMPARE(saleLines()->item(0, 4)->text(), QStringLiteral("2.200"));

    scan(QStringLiteral("7840002"));                    // not in it
    QVERIFY(saleLines()->item(1, 4)->text().isEmpty());

    whenDialogOpens([](QDialog *payment) { enterUntilCharged(payment); });
    press(Qt::Key_F12);
    QCOMPARE(saleLines()->rowCount(), 0);
    const SaleRecord sale = m_core.sales->sessionSales(m_core.cash->currentSession()->id).first();
    const SaleRecord full = *m_core.sales->sale(sale.id);
    QCOMPARE(full.totals.items.first().promotionName, QStringLiteral("PROMO 7840001"));
    QCOMPARE(full.totals.items.first().promotionDiscount, Money(2200));
    QCOMPARE(full.totals.items.last().promotionDiscount, Money(0));
}

void tst_Modules::aPromotionFollowsTheQuantity()
{
    QVERIFY(showSales());
    QVERIFY(percentOff(QStringLiteral("7840001"), 10, AguaraQuantity::whole(3)));   // from 3
    QVERIFY(openTill());

    scan(QStringLiteral("7840001"));
    QVERIFY(saleLines()->item(0, 4)->text().isEmpty());
    scan(QStringLiteral("2*7840001"));
    QCOMPARE(saleLines()->item(0, 4)->text(), QStringLiteral("3.300"));     // now it reaches

    whenDialogOpens([](QDialog *dialog) {
        dialog->findChild<QLineEdit *>()->setText(QStringLiteral("2"));
        dialog->accept();
    });
    press(Qt::Key_F2);
    QVERIFY(saleLines()->item(0, 4)->text().isEmpty());                     // and no longer
}

void tst_Modules::aPromotionDoesntUseTheCashiersLimit()
{
    QVERIFY(showSales());
    QVERIFY(percentOff(QStringLiteral("7840001"), 50));
    QVERIFY(openTill());
    int asked = 0;
    m_users->setSupervisorPrompt([&](const QString &, const QString &, const QString &, QWidget *, QString *, QString *) {
        ++asked;
        return false;
    });
    scan(QStringLiteral("7840001"));                    // 11.000, 5.500 with the promotion
    QCOMPARE(asked, 0);

    whenDialogOpens([](QDialog *dialog) {
        dialog->findChild<QLineEdit *>(QStringLiteral("discountEdit"))->setText(QStringLiteral("5"));
        dialog->accept();
    });
    press(Qt::Key_F4);                                  // 5 % of what is left, a cashier's limit
    QCOMPARE(asked, 0);
    QCOMPARE(saleLines()->item(0, 4)->text(), QStringLiteral("5.775"));     // 5.500 + 275
    QCOMPARE(saleLines()->item(0, 5)->text(), QStringLiteral("5.225"));

    whenDialogOpens([](QDialog *dialog) {
        dialog->findChild<QLineEdit *>(QStringLiteral("discountEdit"))->setText(QStringLiteral("10"));
        dialog->accept();
    });
    press(Qt::Key_F4);                                  // past the limit: a supervisor, as always
    QCOMPARE(asked, 1);
    QCOMPARE(saleLines()->item(0, 4)->text(), QStringLiteral("5.775"));
}

void tst_Modules::aHeldSaleGetsTodaysPromotions()
{
    QVERIFY(showSales());
    QVERIFY(openTill());
    scan(QStringLiteral("7840001"));
    QVERIFY(saleLines()->item(0, 4)->text().isEmpty());
    press(Qt::Key_F6);

    QVERIFY(percentOff(QStringLiteral("7840001"), 10));
    whenDialogOpens([](QDialog *held) { held->accept(); });
    press(Qt::Key_F7);
    QCOMPARE(saleLines()->item(0, 4)->text(), QStringLiteral("1.100"));

    press(Qt::Key_F6);                                  // and the other way around
    QVERIFY(m_core.promotions->setPromotionActive(m_core.promotions->promotions().first().id, false));
    whenDialogOpens([](QDialog *held) { held->accept(); });
    press(Qt::Key_F7);
    QVERIFY(saleLines()->item(0, 4)->text().isEmpty());
}

void tst_Modules::anOutageKeepsThePromotionsKnown()
{
    QVERIFY(showSales());
    QVERIFY(percentOff(QStringLiteral("7840001"), 10));
    QVERIFY(openTill());
    scan(QStringLiteral("7840001"));
    QCOMPARE(saleLines()->item(0, 4)->text(), QStringLiteral("1.100"));

    m_core.monitor->simulateOutage(true);
    QVERIFY(TestSupport::waitFor([&] { return !label("saleConnection")->isHidden(); }, 2500));
    whenDialogOpens([](QDialog *dialog) {
        dialog->findChild<QLineEdit *>()->setText(QStringLiteral("3"));
        dialog->accept();
    });
    press(Qt::Key_F2);
    QCOMPARE(saleLines()->item(0, 4)->text(), QStringLiteral("3.300"));     // it follows, with no server
    m_core.monitor->simulateOutage(false);
}

void tst_Modules::aPromotionReachesTheTicket()
{
    QVERIFY(showSales());
    PrinterSettings printer;
    printer.kind = PrinterKind::Thermal;
    printer.name = QStringLiteral("TERMICA");
    m_core.print->savePrinterSettings(printer);
    QVERIFY(percentOff(QStringLiteral("7840001"), 10));
    QVERIFY(openTill());
    scan(QStringLiteral("7840001"));
    whenDialogOpens([](QDialog *payment) { enterUntilCharged(payment); });
    press(Qt::Key_F12);
    QCOMPARE(m_core.printed.size(), 1);
    QVERIFY(m_core.printed.first().contains("Promo: PROMO 7840001"));
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
    whenDialogOpens([](QDialog *ask) { ask->reject(); });
    m_page->findChild<QPushButton *>(QStringLiteral("customerToggle"))->click();
    QCOMPARE(asked, 0);                                 // no one is called for nothing

    whenDialogOpens([](QDialog *ask) { ask->accept(); });
    m_page->findChild<QPushButton *>(QStringLiteral("customerToggle"))->click();
    QCOMPARE(asked, 1);
    QCOMPARE(m_core.customers->customers().size(), 1);
}

void tst_Modules::inventoryNarrowsAsYouType()
{
    QLineEdit *search = nullptr;
    QTableView *table = showInventory(&search);
    QVERIFY(table && search);
    const int all = table->model()->rowCount();
    QVERIFY(all > 1);

    search->setFocus();
    QTest::keyClicks(search, QStringLiteral("surtidas galletitas"));
    QTRY_COMPARE(table->model()->rowCount(), 1);
    QCOMPARE(table->model()->index(0, int(ProductColumn::Description)).data().toString(),
             QStringLiteral("GALLETITAS SURTIDAS"));

    QTest::keyClick(search, Qt::Key_Escape);
    QVERIFY(search->text().isEmpty());
    QTRY_COMPARE(table->model()->rowCount(), all);

    // a second barcode isn't in the table's columns: Enter brings its article
    QVERIFY(m_core.inventory->addBarcode(QStringLiteral("7840001"), QStringLiteral("555666")));
    QTest::keyClicks(search, QStringLiteral("555666"));
    QTest::keyClick(search, Qt::Key_Return);
    QCOMPARE(table->model()->rowCount(), 1);
    QCOMPARE(table->model()->index(table->currentIndex().row(), int(ProductColumn::Code)).data().toString(),
             QStringLiteral("7840001"));
}

void tst_Modules::articleSearchFindsCodesToo()
{
    QVERIFY(showSales());
    QVERIFY(openTill());
    bool checked = false;
    whenDialogOpens([&](QDialog *dialog) {
        auto table = dialog->findChild<QTableView *>(QStringLiteral("articleTable"));
        auto filter = dialog->findChild<QLineEdit *>(QStringLiteral("articleFilter"));
        filter->setText(QStringLiteral("7840001"));
        QCOMPARE(table->model()->rowCount(), 1);
        filter->setText(QStringLiteral("no existe"));
        QCOMPARE(table->model()->rowCount(), 0);
        checked = true;
    });
    press(Qt::Key_F3);
    QVERIFY(checked);
}

void tst_Modules::tillSuggestsArticles()
{
    QVERIFY(showSales());
    QVERIFY(openTill());
    auto entry = saleEntry();
    QApplication::setActiveWindow(entry->window());     // the list opens only for the field with the focus
    entry->setFocus();
    QVERIFY(entry->hasFocus());
    auto list = entry->findChild<QListWidget *>(QStringLiteral("suggestions"));
    QVERIFY(list);

    QTest::keyClicks(entry, QStringLiteral("3*arti"));
    QTRY_VERIFY(list->isVisible());
    QVERIFY(entry->hasFocus());                         // the cursor stays in the field
    QVERIFY(list->item(0)->text().contains(QStringLiteral("ARTICULO")));

    QTest::keyClick(entry, Qt::Key_Down);
    QCOMPARE(saleLines()->rowCount(), 0);               // not the lines of the sale
    QTest::keyClick(entry, Qt::Key_Return);
    QVERIFY(!list->isVisible());
    QCOMPARE(saleLines()->rowCount(), 1);
    QVERIFY(saleLines()->item(0, 1)->text().contains(QStringLiteral("ARTICULO")));
    QCOMPARE(saleLines()->item(0, 2)->text(), QStringLiteral("3"));
    QVERIFY(entry->text().isEmpty());

    // Esc closes the list, and leaves the sale alone
    QTest::keyClicks(entry, QStringLiteral("arti"));
    QTRY_VERIFY(list->isVisible());
    QTest::keyClick(entry, Qt::Key_Escape);
    QVERIFY(!list->isVisible());
    QCOMPARE(entry->text(), QStringLiteral("arti"));
    QCOMPARE(saleLines()->rowCount(), 1);
}

void tst_Modules::scanningNeverOpensTheSuggestions()
{
    QVERIFY(showSales());
    QVERIFY(openTill());
    auto entry = saleEntry();
    QApplication::setActiveWindow(entry->window());     // the list opens only for the field with the focus
    entry->setFocus();
    QVERIFY(entry->hasFocus());
    auto list = entry->findChild<QListWidget *>(QStringLiteral("suggestions"));

    scan(QStringLiteral("7840001"));                    // digits and Enter
    QTest::qWait(250);
    QVERIFY(!list->isVisible());
    QCOMPARE(saleLines()->rowCount(), 1);

    QTest::keyClicks(entry, QStringLiteral("7840"));    // digits alone never open it either
    QTest::qWait(250);
    QVERIFY(!list->isVisible());
}

void tst_Modules::suppliersNarrowAsYouType()
{
    QLineEdit *search = nullptr;
    QVERIFY(showInventory(&search));
    Supplier one, other;
    one.name = QStringLiteral("DISTRIBUIDORA NÚÑEZ");
    other.name = QStringLiteral("MAYORISTA DEL SUR");
    QVERIFY(m_core.inventory->saveSupplier(one));
    QVERIFY(m_core.inventory->saveSupplier(other));
    m_page->findChild<QTabWidget *>(QStringLiteral("inventorySections"))->setCurrentIndex(2);

    auto table = m_page->findChild<QTableWidget *>(QStringLiteral("suppliersTable"));
    QCOMPARE(table->rowCount(), 2);
    const auto shown = [table]() {
        int count = 0;
        for (int row = 0; row < table->rowCount(); ++row)
            count += table->isRowHidden(row) ? 0 : 1;
        return count;
    };
    m_page->findChild<QLineEdit *>(QStringLiteral("supplierSearch"))->setText(QStringLiteral("nunez"));
    QCOMPARE(shown(), 1);
}

void tst_Modules::altaDeProductos()
{
    QLineEdit *search = nullptr;
    QVERIFY(showInventory(&search));
    Supplier supplier;
    supplier.name = QStringLiteral("DISTRIBUIDORA");
    QVERIFY(m_core.inventory->saveSupplier(supplier));
    const Quantity before = m_core.products->product(QStringLiteral("7840001"))->stock;
    const Money cost = m_core.products->product(QStringLiteral("7840001"))->cost;

    bool saved = false, offered = false;
    whenDialogOpens([&](QDialog *dialog) {
        auto entry = dialog->findChild<QLineEdit *>(QStringLiteral("purchaseEntry"));
        auto lines = dialog->findChild<QTableWidget *>(QStringLiteral("purchaseLines"));
        const auto scan = [entry](const QString &code) {
            entry->setFocus();
            QTest::keyClicks(entry, code);
            QTest::keyClick(entry, Qt::Key_Return);
        };
        scan(QStringLiteral("7840001"));
        scan(QStringLiteral("7840001"));
        QCOMPARE(lines->rowCount(), 1);
        QCOMPARE(lines->item(0, 2)->text(), QStringLiteral("2"));

        lines->setCurrentItem(nullptr);
        lines->item(0, 3)->setText(QStringLiteral("10.000"));   // the margin follows the cost
        lines->item(0, 5)->setText(QStringLiteral("20.000"));
        QCOMPARE(lines->item(0, 4)->text(), QLocale().toString(50.0, 'f', 1));
        lines->item(0, 4)->setText(QStringLiteral("abc"));      // not a margin: it goes back
        QCOMPARE(lines->item(0, 4)->text(), QLocale().toString(50.0, 'f', 1));
        QCOMPARE(lines->item(0, 5)->text(), QStringLiteral("20.000"));
        lines->item(0, 4)->setText(QStringLiteral("60"));       // 10.000 / 0,40
        QCOMPARE(lines->item(0, 5)->text(), QStringLiteral("25.000"));
        saved = true;

        whenDialogOpens([&](QDialog *labels) {
            QCOMPARE(labels->objectName(), QStringLiteral("entryLabels"));
            auto table = labels->findChild<QTableWidget *>(QStringLiteral("entryLabelsTable"));
            QCOMPARE(table->rowCount(), 1);
            QCOMPARE(qobject_cast<QSpinBox *>(table->cellWidget(0, 2))->value(), 2);    // one per unit
            QCOMPARE(table->item(0, 1)->text(), QStringLiteral("25.000"));              // the price just set
            qobject_cast<QSpinBox *>(table->cellWidget(0, 2))->setValue(0);
            labels->accept();
            QVERIFY(labels->isVisible());                                               // nothing to print
            QVERIFY(!labels->findChild<QLabel *>(QStringLiteral("entryLabelsError"))->isHidden());

            qobject_cast<QSpinBox *>(table->cellWidget(0, 2))->setValue(3);
            whenDialogOpens([](QDialog *preview) { preview->reject(); });               // the sheet, ready to print
            labels->accept();
            QVERIFY(!labels->isVisible());
            offered = true;
        });
        dialog->accept();
    });
    m_page->findChild<QPushButton *>(QStringLiteral("receiveButton"))->click();
    QVERIFY(saved);
    QVERIFY(offered);

    const auto article = m_core.products->product(QStringLiteral("7840001"));
    QCOMPARE(article->stock, before + AguaraQuantity::whole(2));
    QCOMPARE(article->price, Money(25000));
    QCOMPARE(article->cost, Money(10000));
    QVERIFY(article->cost != cost);
}

void tst_Modules::altaCreatesTheArticleOnTheSpot()
{
    QLineEdit *search = nullptr;
    QVERIFY(showInventory(&search));
    Supplier supplier;
    supplier.name = QStringLiteral("DISTRIBUIDORA");
    QVERIFY(m_core.inventory->saveSupplier(supplier));

    QString generated;
    bool done = false;
    whenDialogOpens([&](QDialog *dialog) {
        auto lines = dialog->findChild<QTableWidget *>(QStringLiteral("purchaseLines"));
        const auto form = [dialog]() {
            for (QDialog *d : dialog->findChildren<QDialog *>()) {
                if (d->objectName() == QLatin1String("addItemDialog"))
                    return d;
            }
            return static_cast<QDialog *>(nullptr);
        };
        const auto fill = [&](const QString &description) {
            form()->findChild<QLineEdit *>(QStringLiteral("lineEdit_description"))->setText(description);
            form()->findChild<QLineEdit *>(QStringLiteral("lineEdit_price"))->setText(QStringLiteral("7.000"));
            form()->findChild<QLineEdit *>(QStringLiteral("lineEdit_cost"))->setText(QStringLiteral("5.000"));
            form()->accept();
        };

        // a code nobody knows: the form opens with it, without a stock of its own
        auto entry = dialog->findChild<QLineEdit *>(QStringLiteral("purchaseEntry"));
        entry->setFocus();
        QTest::keyClicks(entry, QStringLiteral("7799001"));
        QTest::keyClick(entry, Qt::Key_Return);
        QVERIFY(form()->isVisible());
        QCOMPARE(form()->findChild<QLineEdit *>(QStringLiteral("lineEdit_code"))->text(), QStringLiteral("7799001"));
        QVERIFY(form()->findChild<QLineEdit *>(QStringLiteral("lineEdit_stock"))->isReadOnly());
        fill(QStringLiteral("ARTICULO CON CODIGO"));
        QCOMPARE(lines->rowCount(), 1);

        // the button, and a code of the shop's own
        dialog->findChild<QPushButton *>(QStringLiteral("purchaseNewArticle"))->click();
        QVERIFY(form()->isVisible());
        form()->findChild<QPushButton *>(QStringLiteral("generateCode"))->click();
        generated = form()->findChild<QLineEdit *>(QStringLiteral("lineEdit_code"))->text();
        fill(QStringLiteral("ARTICULO SIN CODIGO"));
        QCOMPARE(lines->rowCount(), 2);
        QCOMPARE(lines->item(1, 0)->text(), generated);
        QCOMPARE(lines->item(1, 5)->text(), QStringLiteral("7.000"));

        lines->setCurrentItem(nullptr);
        lines->item(1, 2)->setText(QStringLiteral("5"));
        done = true;
        whenDialogOpens([](QDialog *labels) { labels->reject(); });
        dialog->accept();
    });
    m_page->findChild<QPushButton *>(QStringLiteral("receiveButton"))->click();
    QVERIFY(done);

    QVERIFY(generated.startsWith(QStringLiteral("299")));
    QVERIFY(AguaraBarcode::isValidEan13(generated));
    QCOMPARE(m_core.products->product(generated)->stock, AguaraQuantity::whole(5));     // only what came in
    QCOMPARE(m_core.products->product(QStringLiteral("7799001"))->stock, AguaraQuantity::whole(1));
    QCOMPARE(m_core.inventory->movements(generated).size(), 1);
}

void tst_Modules::altaCreatesTheSupplierOnTheSpot()
{
    QLineEdit *search = nullptr;
    QVERIFY(showInventory(&search));
    QVERIFY(m_core.inventory->suppliers().isEmpty());       // the first alta of a new shop

    bool done = false;
    whenDialogOpens([&](QDialog *dialog) {
        auto supplier = dialog->findChild<QComboBox *>(QStringLiteral("purchaseSupplier"));
        auto button = dialog->findChild<QPushButton *>(QStringLiteral("purchaseNewSupplier"));
        auto error = dialog->findChild<QLabel *>(QStringLiteral("purchaseError"));
        QCOMPARE(supplier->count(), 0);

        auto entry = dialog->findChild<QLineEdit *>(QStringLiteral("purchaseEntry"));
        entry->setFocus();
        QTest::keyClicks(entry, QStringLiteral("7840001"));
        QTest::keyClick(entry, Qt::Key_Return);
        dialog->findChild<QTableWidget *>(QStringLiteral("purchaseLines"))->setCurrentItem(nullptr);
        dialog->accept();
        QVERIFY(dialog->isVisible());                       // who it comes from can't stay empty
        QVERIFY(error->text().contains(QStringLiteral("proveedor")));

        const auto save = [](QDialog *form) {
            form->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
        };
        whenDialogOpens([&](QDialog *form) {                // no name: the form stays, and nothing changes
            save(form);
            QVERIFY(form->isVisible());
            QVERIFY(!form->findChild<QLabel *>(QStringLiteral("supplierError"))->isHidden());
            form->reject();
        });
        button->click();
        QCOMPARE(supplier->count(), 0);

        whenDialogOpens([&](QDialog *form) {
            form->findChild<QLineEdit *>(QStringLiteral("supplierName"))->setText(QStringLiteral("DISTRIBUIDORA NUEVA"));
            save(form);
        });
        button->click();
        QCOMPARE(supplier->count(), 1);
        QCOMPARE(supplier->currentText(), QStringLiteral("DISTRIBUIDORA NUEVA"));   // already chosen
        QVERIFY(error->isHidden());

        done = true;
        whenDialogOpens([](QDialog *labels) { labels->reject(); });
        dialog->accept();
    });
    m_page->findChild<QPushButton *>(QStringLiteral("receiveButton"))->click();
    QVERIFY(done);

    const QList<Purchase> purchases = m_core.inventory->purchases(QDate::currentDate(), QDate::currentDate());
    QCOMPARE(purchases.size(), 1);
    QCOMPARE(purchases.first().supplierName, QStringLiteral("DISTRIBUIDORA NUEVA"));
}

void tst_Modules::telemetrySeesWhatHappensNotAboutWhom()
{
    QVERIFY(showSales());
    QVERIFY(openTill());

    Customer customer;
    customer.name = QStringLiteral("Ana Secreta");
    customer.documentType = QString::fromLatin1(DocumentType::Ci);
    customer.document = QStringLiteral("7654321");
    customer.phone = QStringLiteral("0981 555 777");
    customer.email = QStringLiteral("ana@secreta.com.py");
    m_core.customers->setUsers(m_users.get());

    QTemporaryDir folder;
    Telemetry telemetry(folder.path());
    TelemetrySettings settings;
    settings.enabled = true;
    settings.endpoint = QStringLiteral("http://127.0.0.1:9");
    settings.key = QStringLiteral("clave");
    telemetry.configure(settings);

    // the real services, with real data in what they audit
    QVERIFY(m_core.customers->saveCustomer(customer) != 0);
    scan(QStringLiteral("7840001"));
    whenDialogOpens([](QDialog *payment) { enterUntilCharged(payment); });
    press(Qt::Key_F12);
    m_users->audit(QStringLiteral("prueba.accion"), QStringLiteral("cliente"), QStringLiteral("7654321"),
                   QStringLiteral("Ana Secreta"), QStringLiteral("Gs. 987.654"));
    m_core.reports->run(QStringLiteral("ventas.periodo"), ReportFilter());

    const QByteArray report = telemetry.report();
    for (const char *happened : { "cliente.crear", "venta.cobrar", "prueba.accion", "reporte.ventas.periodo" })
        QVERIFY2(report.contains(happened), happened);
    for (const char *secret : { "Ana", "Secreta", "7654321", "0981", "secreta.com", "987.654", "cajero", "7840001", "ARTICULO" })
        QVERIFY2(!report.contains(secret), secret);
}

void tst_Modules::emptyEnterDoesNotCharge()
{
    QVERIFY(showSales());
    QVERIFY(openTill());
    scan(QStringLiteral("7840001"));
    QCOMPARE(saleLines()->rowCount(), 1);

    bool checked = false;
    whenDialogOpens([&](QDialog *payment) {
        auto tendered = payment->findChild<QLineEdit *>(QStringLiteral("paymentTendered"));
        QVERIFY(tendered->text().isEmpty());
        QTest::keyClick(tendered, Qt::Key_Return);
        QVERIFY(payment->isVisible());                  // a stray Enter sells nothing
        QCOMPARE(tendered->text(), QStringLiteral("11.000"));
        QCOMPARE(payment->findChild<QLabel *>(QStringLiteral("paymentChange"))->text(), QStringLiteral("Vuelto: Gs. 0"));
        QTest::keyClick(tendered, Qt::Key_Return);
        QVERIFY(!payment->isVisible());
        checked = true;
    });
    press(Qt::Key_F12);
    QVERIFY(checked);
    QCOMPARE(saleLines()->rowCount(), 0);               // charged
}

void tst_Modules::scannerCantPayInCash()
{
    QVERIFY(showSales());
    QVERIFY(openTill());
    scan(QStringLiteral("7840001"));

    bool checked = false;
    whenDialogOpens([&](QDialog *payment) {
        auto tendered = payment->findChild<QLineEdit *>(QStringLiteral("paymentTendered"));
        auto error = payment->findChild<QLabel *>(QStringLiteral("paymentError"));
        QTest::keyClicks(tendered, QStringLiteral("7840001000017"));
        QTest::keyClick(tendered, Qt::Key_Return);      // the scanner's own Enter
        QVERIFY(payment->isVisible());
        QVERIFY(tendered->text().isEmpty());            // no code, and no exact amount proposed either
        QVERIFY(!error->isHidden());

        QTest::qWait(200);                              // then the cashier just types it
        typeByHand(tendered, QStringLiteral("20.000"));
        QVERIFY(error->isHidden());
        QTest::keyClick(tendered, Qt::Key_Return);
        QVERIFY(!payment->isVisible());
        checked = true;
    });
    press(Qt::Key_F12);
    QVERIFY(checked);
    QCOMPARE(saleLines()->rowCount(), 0);
}

void tst_Modules::cashCantBeAbsurd()
{
    QVERIFY(showSales());
    QVERIFY(openTill());
    scan(QStringLiteral("7840001"));

    bool checked = false;
    whenDialogOpens([&](QDialog *payment) {
        auto tendered = payment->findChild<QLineEdit *>(QStringLiteral("paymentTendered"));
        tendered->setText(QStringLiteral("7840001"));   // a scanner too slow to be told from a hand
        payment->accept();
        QVERIFY(payment->isVisible());
        QVERIFY(!payment->findChild<QLabel *>(QStringLiteral("paymentError"))->isHidden());
        tendered->setText(QStringLiteral("500.000"));   // odd, but it can be real
        payment->accept();
        QVERIFY(!payment->isVisible());
        checked = true;
    });
    press(Qt::Key_F12);
    QVERIFY(checked);
}

void tst_Modules::removalAsksAndEnterKeeps()
{
    whenDialogOpens([](QDialog *ask) {
        QCOMPARE(ask->objectName(), QStringLiteral("confirmRemoval"));
        QVERIFY(ask->findChild<QPushButton *>(QStringLiteral("confirmNo"))->isDefault());
        QTest::keyClick(ask, Qt::Key_Return);
    });
    QVERIFY(!AguaraForm::confirmRemoval(nullptr, QStringLiteral("Quitar"), QStringLiteral("Algo"), QStringLiteral("Quitar")));

    whenDialogOpens([](QDialog *ask) { QTest::keyClick(ask, Qt::Key_Escape); });
    QVERIFY(!AguaraForm::confirmRemoval(nullptr, QStringLiteral("Quitar"), QStringLiteral("Algo"), QStringLiteral("Quitar")));

    whenDialogOpens([](QDialog *ask) {                  // with the keyboard: Tab to the button, then Enter
        QApplication::setActiveWindow(ask);
        auto yes = ask->findChild<QPushButton *>(QStringLiteral("confirmYes"));
        QTest::keyClick(ask->focusWidget(), Qt::Key_Tab);
        QCOMPARE(ask->focusWidget(), static_cast<QWidget *>(yes));
        QTest::keyClick(yes, Qt::Key_Return);
    });
    QVERIFY(AguaraForm::confirmRemoval(nullptr, QStringLiteral("Quitar"), QStringLiteral("Algo"), QStringLiteral("Quitar")));

    whenDialogOpens([](QDialog *ask) { ask->findChild<QPushButton *>(QStringLiteral("confirmYes"))->click(); });
    QVERIFY(AguaraForm::confirmRemoval(nullptr, QStringLiteral("Quitar"), QStringLiteral("Algo"), QStringLiteral("Quitar")));
}

void tst_Modules::removingARoleAsks()
{
    openUsers();
    UserRole role;
    role.name = QStringLiteral("TEMPORAL");
    QVERIFY(m_users->saveRole(role));
    const auto named = [this]() {
        for (const UserRole &r : m_users->roles()) {
            if (r.name == QLatin1String("TEMPORAL"))
                return true;
        }
        return false;
    };

    auto widgets = qobject_cast<WidgetModuleInterface *>(load(QStringLiteral("modulo_usuarios.dll")));
    QVERIFY(widgets);
    std::unique_ptr<QWidget> page(widgets->createWidget());
    auto list = page->findChild<QListWidget *>(QStringLiteral("rolesList"));
    const QList<QListWidgetItem *> found = list->findItems(QStringLiteral("TEMPORAL"), Qt::MatchExactly);
    QCOMPARE(found.size(), 1);
    list->setCurrentItem(found.first());

    auto remove = page->findChild<QPushButton *>(QStringLiteral("roleRemove"));
    whenDialogOpens([](QDialog *ask) { ask->reject(); });
    remove->click();
    QVERIFY(named());
    whenDialogOpens([](QDialog *ask) { ask->accept(); });
    remove->click();
    QVERIFY(!named());
}

void tst_Modules::deactivatingAUserAsks()
{
    openUsers();
    const auto cashier = [this]() {
        for (const UserAccount &u : m_users->users()) {
            if (u.username == QLatin1String("cajero"))
                return u;
        }
        return UserAccount();
    };
    auto widgets = qobject_cast<WidgetModuleInterface *>(load(QStringLiteral("modulo_usuarios.dll")));
    QVERIFY(widgets);
    std::unique_ptr<QWidget> page(widgets->createWidget());
    auto table = page->findChild<QTableWidget *>(QStringLiteral("usersTable"));
    int row = -1;
    for (int i = 0; i < table->rowCount(); ++i) {
        if (table->item(i, 1)->text() == QLatin1String("cajero"))
            row = i;
    }
    QVERIFY(row >= 0);
    table->selectRow(row);

    whenDialogOpens([&](QDialog *form) {
        form->findChild<QCheckBox *>(QStringLiteral("userActive"))->setChecked(false);
        whenDialogOpens([](QDialog *ask) { ask->reject(); });
        form->accept();
        QVERIFY(form->isVisible());
        QVERIFY(cashier().active);
        whenDialogOpens([](QDialog *ask) { ask->accept(); });
        form->accept();
    });
    page->findChild<QPushButton *>(QStringLiteral("userEdit"))->click();
    QVERIFY(!cashier().active);
}

void tst_Modules::articleFormSaysWhatIsMissing()
{
    QLineEdit *search = nullptr;
    QVERIFY(showInventory(&search));
    m_page->findChild<QPushButton *>(QStringLiteral("itemAddButton"))->click();
    QDialog *form = nullptr;
    for (QDialog *d : m_page->findChildren<QDialog *>()) {
        if (d->isVisible())
            form = d;
    }
    QVERIFY(form);
    auto error = form->findChild<QLabel *>(QStringLiteral("itemError"));
    const auto set = [form](const char *name, const QString &text) {
        form->findChild<QLineEdit *>(QLatin1String(name))->setText(text);
    };
    const auto refuses = [&](const QString &word) {
        error->hide();
        form->accept();
        return form->isVisible() && !error->isHidden() && error->text().contains(word);
    };

    QVERIFY(refuses(QStringLiteral("código")));
    set("lineEdit_code", QStringLiteral("555"));
    QVERIFY(refuses(QStringLiteral("descripción")));
    set("lineEdit_description", QStringLiteral("ARTICULO NUEVO"));
    QVERIFY(refuses(QStringLiteral("precio")));
    set("lineEdit_price", QStringLiteral("0"));
    QVERIFY(refuses(QStringLiteral("mayor que cero")));         // it would sell for free
    set("lineEdit_price", QStringLiteral("5.000"));
    set("lineEdit_stock", QStringLiteral("1,5"));
    QVERIFY(refuses(QStringLiteral("decimales")));              // sold by the unit
    QVERIFY(!m_core.products->product(QStringLiteral("555")));

    set("lineEdit_stock", QStringLiteral("2"));
    form->accept();
    QVERIFY(!form->isVisible());
    QCOMPARE(m_core.products->product(QStringLiteral("555"))->price, Money(5000));
}

void tst_Modules::articleFormManagesUnitsAndTheUseByDate()
{
    QLineEdit *search = nullptr;
    QTableView *table = showInventory(&search);
    QVERIFY(table);
    m_page->findChild<QPushButton *>(QStringLiteral("itemAddButton"))->click();
    QDialog *form = nullptr;
    for (QDialog *d : m_page->findChildren<QDialog *>()) {
        if (d->isVisible())
            form = d;
    }
    QVERIFY(form);
    auto units = form->findChild<QComboBox *>(QStringLiteral("comboBox_unit"));
    const int before = units->count();

    form->findChild<QToolButton *>(QStringLiteral("button_modifyUnitTable"))->click();
    auto list = form->findChild<QTableWidget *>(QStringLiteral("unitTable"));
    QVERIFY(list);
    QCOMPARE(list->rowCount(), before);
    auto add = form->findChild<QPushButton *>(QStringLiteral("unitAdd"));
    auto error = form->findChild<QLabel *>(QStringLiteral("unitError"));
    form->findChild<QLineEdit *>(QStringLiteral("unitName"))->setText(QStringLiteral("Caja"));
    add->click();
    QVERIFY(!error->isHidden());                                // no abbreviation
    QCOMPARE(units->count(), before);
    form->findChild<QLineEdit *>(QStringLiteral("unitAbbreviation"))->setText(QStringLiteral("cj"));
    form->findChild<QSpinBox *>(QStringLiteral("unitDecimals"))->setValue(2);
    add->click();
    QCOMPARE(list->rowCount(), before + 1);
    QCOMPARE(units->count(), before + 1);                       // already in the article's list

    // the basic unit stays, and says why
    auto remove = form->findChild<QPushButton *>(QStringLiteral("unitRemove"));
    list->setCurrentCell(0, 0);
    whenDialogOpens([](QDialog *confirm) { confirm->accept(); });
    remove->click();
    QVERIFY(!error->isHidden());
    QCOMPARE(list->rowCount(), before + 1);

    list->setCurrentCell(before - 1, 0);                        // Metro
    whenDialogOpens([](QDialog *confirm) { confirm->accept(); });
    remove->click();
    QCOMPARE(list->rowCount(), before);
    QCOMPARE(units->count(), before);
    list->window()->close();

    form->findChild<QLineEdit *>(QStringLiteral("lineEdit_code"))->setText(QStringLiteral("555"));
    form->findChild<QLineEdit *>(QStringLiteral("lineEdit_description"))->setText(QStringLiteral("HUEVOS"));
    form->findChild<QLineEdit *>(QStringLiteral("lineEdit_price"))->setText(QStringLiteral("5.000"));
    form->findChild<QLineEdit *>(QStringLiteral("lineEdit_stock"))->setText(QStringLiteral("12"));
    units->setCurrentIndex(units->findText(QStringLiteral("Caja")));
    auto expires = form->findChild<QCheckBox *>(QStringLiteral("checkBox_expires"));
    auto date = form->findChild<QDateEdit *>(QStringLiteral("dateEdit_expires"));
    QVERIFY(!expires->isChecked());
    QVERIFY(!date->isEnabled());
    expires->setChecked(true);
    QVERIFY(date->isEnabled());
    date->setDate(QDate(2031, 3, 9));
    form->accept();
    QVERIFY(!form->isVisible());
    const auto saved = m_core.products->product(QStringLiteral("555"));
    QCOMPARE(saved->unit.name, QStringLiteral("Caja"));
    QCOMPARE(saved->unit.decimals, 2);
    QCOMPARE(saved->expires, QDate(2031, 3, 9));
    QCOMPARE(m_core.inventory->lots(QStringLiteral("555")).first().quantity, AguaraQuantity::whole(12));

    // in an article that exists the date belongs to its lots
    table->selectRow(0);
    QCOMPARE(table->model()->index(0, 0).data().toString(), QStringLiteral("555"));
    m_page->findChild<QPushButton *>(QStringLiteral("itemEditButton"))->click();
    form = nullptr;
    for (QDialog *d : m_page->findChildren<QDialog *>()) {
        if (d->isVisible())
            form = d;
    }
    QVERIFY(form);
    QVERIFY(!form->findChild<QCheckBox *>(QStringLiteral("checkBox_expires"))->isVisible());
    form->accept();
    QCOMPARE(m_core.products->product(QStringLiteral("555"))->expires, QDate(2031, 3, 9));
}

void tst_Modules::lotsThroughTheHistory()
{
    QLineEdit *search = nullptr;
    QTableView *table = showInventory(&search);
    QVERIFY(table);
    Product p = *m_core.products->product(QStringLiteral("7840001"));
    p.stock = AguaraQuantity::whole(10);
    QVERIFY(m_core.products->saveProduct(p));
    table->selectRow(0);
    QCOMPARE(table->model()->index(0, 0).data().toString(), p.code);

    bool done = false;
    whenDialogOpens([&](QDialog *dialog) {
        auto lots = dialog->findChild<QTableWidget *>(QStringLiteral("lotTable"));
        auto undated = dialog->findChild<QLabel *>(QStringLiteral("lotUndated"));
        QVERIFY(lots);
        QCOMPARE(lots->rowCount(), 0);
        QVERIFY2(undated->text().contains(QStringLiteral("10")), qPrintable(undated->text()));

        dialog->findChild<QLineEdit *>(QStringLiteral("lotQuantity"))->setText(QStringLiteral("4"));
        dialog->findChild<QDateEdit *>(QStringLiteral("lotDate"))->setDate(QDate(2020, 1, 1));
        dialog->findChild<QPushButton *>(QStringLiteral("lotAdd"))->click();
        QCOMPARE(lots->rowCount(), 1);
        QCOMPARE(lots->item(0, 1)->text(), QStringLiteral("4"));
        QCOMPARE(lots->item(0, 2)->text(), QStringLiteral("Vencido"));
        QVERIFY2(undated->text().contains(QStringLiteral(": 6")), qPrintable(undated->text()));

        lots->setCurrentCell(0, 0);
        whenDialogOpens([](QDialog *confirm) { confirm->accept(); });
        dialog->findChild<QPushButton *>(QStringLiteral("lotRemove"))->click();
        QCOMPARE(lots->rowCount(), 0);
        done = true;
        dialog->reject();
    });
    QVERIFY(QMetaObject::invokeMethod(m_page, "onHistory"));
    QVERIFY(done);
    QVERIFY(m_core.inventory->lots(p.code).isEmpty());
}

void tst_Modules::articleFormKeepsARemovedUnit()
{
    QLineEdit *search = nullptr;
    QTableView *table = showInventory(&search);
    QVERIFY(table);
    Product p;
    p.code = QStringLiteral("555");
    p.description = QStringLiteral("QUESO");
    p.price = 10000;
    p.unit.id = 2;      // Kilogramo
    QVERIFY(m_core.products->saveProduct(p));
    QVERIFY(m_core.products->deactivateProduct(p.code));
    QVERIFY(m_core.products->removeUnit(2));
    p = *m_core.products->product(p.code);
    QVERIFY(m_core.products->saveProduct(p));   // back in use, with the unit it had

    table->selectRow(0);
    m_page->findChild<QPushButton *>(QStringLiteral("itemEditButton"))->click();
    QDialog *form = nullptr;
    for (QDialog *d : m_page->findChildren<QDialog *>()) {
        if (d->isVisible())
            form = d;
    }
    QVERIFY(form);
    QCOMPARE(form->findChild<QComboBox *>(QStringLiteral("comboBox_unit"))->currentText(), QStringLiteral("Kilogramo"));
    form->accept();
    QCOMPARE(m_core.products->product(p.code)->unit.id, qint64(2));
}

void tst_Modules::countRefusesWhatItCantRead()
{
    QLineEdit *search = nullptr;
    QVERIFY(showInventory(&search));
    m_page->findChild<QTabWidget *>(QStringLiteral("inventorySections"))->setCurrentIndex(3);
    m_page->findChild<QPushButton *>(QStringLiteral("countPrepare"))->click();
    auto table = m_page->findChild<QTableWidget *>(QStringLiteral("countTable"));
    QVERIFY(table->rowCount() > 0);

    for (const QString &typed : { QStringLiteral("12x"), QStringLiteral("-1"), QStringLiteral("1,5") }) {
        table->item(0, 3)->setText(typed);
        QCOMPARE(table->item(0, 4)->text(), QStringLiteral("revisar"));
        m_page->findChild<QPushButton *>(QStringLiteral("countApply"))->click();    // it would hang on a question
        QVERIFY(m_page->findChild<QLabel *>(QStringLiteral("countSummary"))->text().contains(QStringLiteral("Revise")));
        QCOMPARE(table->currentRow(), 0);
    }
}

void tst_Modules::purchaseChecksItsLines()
{
    QLineEdit *search = nullptr;
    QVERIFY(showInventory(&search));
    Supplier supplier;
    supplier.name = QStringLiteral("DISTRIBUIDORA");
    QVERIFY(m_core.inventory->saveSupplier(supplier));
    m_page->findChild<QTabWidget *>(QStringLiteral("inventorySections"))->setCurrentIndex(1);

    bool checked = false;
    whenDialogOpens([&](QDialog *dialog) {
        QVERIFY(dialog->findChild<QDateEdit *>(QStringLiteral("purchaseDate"))->maximumDate() == QDate::currentDate());
        auto entry = dialog->findChild<QLineEdit *>(QStringLiteral("purchaseEntry"));
        QTest::keyClicks(entry, QStringLiteral("7840001"));
        QTest::keyClick(entry, Qt::Key_Return);
        auto lines = dialog->findChild<QTableWidget *>(QStringLiteral("purchaseLines"));
        auto error = dialog->findChild<QLabel *>(QStringLiteral("purchaseError"));
        for (const QString &quantity : { QStringLiteral("0"), QStringLiteral("-2"), QStringLiteral("x"), QStringLiteral("1,5") }) {
            lines->closePersistentEditor(lines->item(0, 2));
            lines->setCurrentItem(nullptr);
            lines->item(0, 2)->setText(quantity);
            error->hide();
            dialog->accept();
            QVERIFY(dialog->isVisible());
            QVERIFY(!error->isHidden());
        }
        checked = true;
    });
    m_page->findChild<QPushButton *>(QStringLiteral("newPurchaseButton"))->click();
    QVERIFY(checked);
    QVERIFY(m_core.inventory->purchases(QDate::currentDate(), QDate::currentDate()).isEmpty());
}

void tst_Modules::priceChangeNeedsSomethingToChange()
{
    QLineEdit *search = nullptr;
    QVERIFY(showInventory(&search));
    bool checked = false;
    whenDialogOpens([&](QDialog *dialog) {
        auto apply = dialog->findChild<QPushButton *>(QStringLiteral("priceApply"));
        QVERIFY(apply->isEnabled());
        dialog->findChild<QDoubleSpinBox *>()->setValue(0);
        QVERIFY(!apply->isEnabled());
        QVERIFY(dialog->findChild<QLabel *>(QStringLiteral("priceSummary"))->text().contains(QStringLiteral("0 %")));
        checked = true;
    });
    QMenu *menu = m_page->findChild<QPushButton *>(QStringLiteral("moreButton"))->menu();
    for (QAction *action : menu->actions()) {
        if (action->text().startsWith(QStringLiteral("Cambio de precios")))
            action->trigger();
    }
    QVERIFY(checked);
}

void tst_Modules::cashMovementSaysWhatIsMissing()
{
    QVERIFY(showSales());
    QVERIFY(openTill());
    m_users->setSupervisorPrompt([](const QString &, const QString &, const QString &, QWidget *,
                                    QString *username, QString *password) {
        *username = QStringLiteral("encargado");
        *password = QStringLiteral("clave123");
        return true;
    });
    bool checked = false;
    whenDialogOpens([&](QDialog *dialog) {
        auto error = dialog->findChild<QLabel *>(QStringLiteral("movementError"));
        dialog->accept();
        QVERIFY(dialog->isVisible());
        QVERIFY(error->text().contains(QStringLiteral("monto")));
        dialog->findChild<QLineEdit *>(QStringLiteral("movementAmount"))->setText(QStringLiteral("10.000"));
        dialog->accept();
        QVERIFY(dialog->isVisible());
        QVERIFY(error->text().contains(QStringLiteral("motivo")));
        checked = true;
    });
    trigger("actionCashMovement");
    QVERIFY(checked);
}

void tst_Modules::removingAVoucherAsks()
{
    QVERIFY(showSales());
    QVERIFY(openTill());
    scan(QStringLiteral("7840001"));
    int left = -1;
    whenDialogOpens([&](QDialog *payment) {
        whenDialogOpens([](QDialog *voucher) { fillVoucher(voucher, QStringLiteral("1")); });
        QTest::keyClick(payment, Qt::Key_F2);
        auto vouchers = payment->findChild<QTableWidget *>(QStringLiteral("paymentVouchers"));
        QCOMPARE(vouchers->rowCount(), 1);
        auto remove = payment->findChild<QPushButton *>(QStringLiteral("paymentRemove"));
        whenDialogOpens([](QDialog *ask) { ask->reject(); });
        remove->click();
        QCOMPARE(vouchers->rowCount(), 1);
        whenDialogOpens([](QDialog *ask) { ask->accept(); });
        remove->click();
        left = vouchers->rowCount();
    });
    press(Qt::Key_F12);
    QCOMPARE(left, 0);
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
    whenDialogOpens([](QDialog *payment) { enterUntilCharged(payment); });
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
    whenDialogOpens([](QDialog *payment) { enterUntilCharged(payment); });
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
    whenDialogOpens([](QDialog *payment) { enterUntilCharged(payment); });
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
    whenDialogOpens([](QDialog *payment) { enterUntilCharged(payment); });
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
