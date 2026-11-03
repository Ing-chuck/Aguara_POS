// The real module plugins (built into <build>/bin/modules), driven through
// the same host and services the application gives them, on SQLite.
#include "testsupport.h"

#include <interfaces.h>

#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QPluginLoader>
#include <QTableView>
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

private:
    QObject *load(const QString &fileName);
    QString writeFile(const QString &name, const QByteArray &bytes);
    QTableView *showInventory(QLineEdit **search);

    QTemporaryDir m_tmp;
    CoreFixture m_core;
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
    existing.price = 15000; existing.cost = 11000; existing.stock = 24;
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
    QCOMPARE(updated->stock, 24);               // not in the file: kept
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
