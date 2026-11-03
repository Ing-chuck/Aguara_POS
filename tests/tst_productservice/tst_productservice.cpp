// ProductService (what modules use for articles), on SQLite and PostgreSQL.
#include "testsupport.h"

#include <QAbstractItemModel>
#include <QtTest>

using TestSupport::CoreFixture;

class tst_ProductService : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase_data() { TestSupport::addBackendRows(); }
    void cleanup() { m_core.close(); }

    void foundThroughTheHost();
    void vatCategories();
    void saveAndRead();
    void bigPrices();
    void modelRefreshesAndFormats();
    void editAndRename();
    void renameOntoExistingCodeFails();
    void validation_data();
    void validation();
    void deactivateAndReactivate();
    void brandsIgnoreCase();
    void removedBrandStaysOnProducts();
    void importIsAllOrNothing();
    void savingLogsNoDatabaseWarnings();

private:
    ProductService *products() { return m_core.host->service<ProductService>(); }
    static Product gaseosa();

    QTemporaryDir m_tmp;
    CoreFixture m_core;
};

Product tst_ProductService::gaseosa()
{
    Product p;
    p.code = QStringLiteral("0012345678905");       // leading zeros must survive
    p.description = QStringLiteral("GASEOSA 2L");
    p.brandName = QStringLiteral("COCA COLA");
    p.categoryName = QStringLiteral("BEBIDAS");
    p.price = 15000;
    p.cost = 11000;
    p.stock = 24;
    p.vatRate = 10;
    p.purchased = QDate(2026, 10, 31);
    return p;
}

void tst_ProductService::foundThroughTheHost()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QCOMPARE(products(), static_cast<ProductService *>(m_core.products.get()));
}

void tst_ProductService::vatCategories()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    const QList<VatCategory> vat = products()->vatCategories();
    QCOMPARE(vat.size(), 3);
    QCOMPARE(vat.first().rate, 10);
}

void tst_ProductService::saveAndRead()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY2(products()->saveProduct(gaseosa()), qPrintable(products()->lastError()));
    const auto got = products()->product(QStringLiteral("0012345678905"));
    QVERIFY(got.has_value());
    QCOMPARE(got->code, QStringLiteral("0012345678905"));
    QCOMPARE(got->description, QStringLiteral("GASEOSA 2L"));
    QCOMPARE(got->price, Money(15000));
    QCOMPARE(got->cost, Money(11000));
    QCOMPARE(got->stock, 24);
    QCOMPARE(got->brandName, QStringLiteral("COCA COLA"));
    QCOMPARE(got->categoryName, QStringLiteral("BEBIDAS"));
    QCOMPARE(got->purchased, QDate(2026, 10, 31));
    QCOMPARE(got->modified, QDate::currentDate());
    QVERIFY(got->active);
    QVERIFY(!products()->product(QStringLiteral("nope")).has_value());
}

void tst_ProductService::bigPrices()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    Product p = gaseosa();
    p.price = Money(3000000000LL);              // more than 32 bits
    QVERIFY(products()->saveProduct(p));
    QCOMPARE(products()->product(p.code)->price, Money(3000000000LL));
}

void tst_ProductService::modelRefreshesAndFormats()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QAbstractItemModel *model = products()->createProductTableModel(this);
    QCOMPARE(model->rowCount(), 0);
    QVERIFY(products()->saveProduct(gaseosa()));
    QCOMPARE(model->rowCount(), 1);             // refreshed by the save
    QCOMPARE(model->index(0, int(ProductColumn::Price)).data().toString(), QStringLiteral("15.000"));
    QCOMPARE(model->index(0, int(ProductColumn::Vat)).data().toString(), QStringLiteral("10%"));
    QCOMPARE(model->index(0, int(ProductColumn::Purchased)).data().toString(), QStringLiteral("31/10/2026"));
    QCOMPARE(model->index(0, int(ProductColumn::Code)).data().toString(), QStringLiteral("0012345678905"));
    delete model;
}

void tst_ProductService::editAndRename()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    Product p = gaseosa();
    QVERIFY(products()->saveProduct(p));
    p.description = QStringLiteral("GASEOSA 2.25L");
    QVERIFY(products()->saveProduct(p, p.code));                // edit, same code
    QCOMPARE(products()->product(p.code)->description, QStringLiteral("GASEOSA 2.25L"));

    const QString old = p.code;
    p.code = QStringLiteral("777");
    QVERIFY(products()->saveProduct(p, old));                   // rename
    QVERIFY(!products()->product(old).has_value());
    QCOMPARE(products()->product(QStringLiteral("777"))->description, QStringLiteral("GASEOSA 2.25L"));
}

void tst_ProductService::renameOntoExistingCodeFails()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    Product a = gaseosa();
    Product b; b.code = QStringLiteral("888"); b.description = QStringLiteral("AGUA"); b.price = 5000; b.vatRate = 5;
    QVERIFY(products()->saveProduct(a));
    QVERIFY(products()->saveProduct(b));
    a.code = QStringLiteral("888");
    QVERIFY(!products()->saveProduct(a, gaseosa().code));
    QCOMPARE(products()->lastError(), QString::fromUtf8("El código 888 ya lo usa otro artículo."));
    QCOMPARE(products()->product(QStringLiteral("888"))->description, QStringLiteral("AGUA"));
}

void tst_ProductService::validation_data()
{
    QTest::addColumn<QString>("code");
    QTest::addColumn<QString>("description");
    QTest::addColumn<qint64>("price");
    QTest::addColumn<int>("vat");
    QTest::addColumn<QString>("message");

    QTest::newRow("no code")        << " "   << "X" << qint64(1)  << 10 << QString::fromUtf8("Falta el código del artículo.");
    QTest::newRow("no description") << "999" << " " << qint64(1)  << 10 << QString::fromUtf8("Falta la descripción del artículo.");
    QTest::newRow("negative price") << "999" << "X" << qint64(-1) << 10 << QString::fromUtf8("El precio y el costo no pueden ser negativos.");
    QTest::newRow("unknown IVA")    << "999" << "X" << qint64(1)  << 7  << QString::fromUtf8("La tasa de IVA 7% no existe.");
}

void tst_ProductService::validation()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QFETCH(QString, code);
    QFETCH(QString, description);
    QFETCH(qint64, price);
    QFETCH(int, vat);
    QFETCH(QString, message);

    Product p; p.code = code; p.description = description; p.price = price; p.vatRate = vat;
    QVERIFY(!products()->saveProduct(p));
    QCOMPARE(products()->lastError(), message);
}

void tst_ProductService::deactivateAndReactivate()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QAbstractItemModel *model = products()->createProductTableModel(this);
    QVERIFY(products()->saveProduct(gaseosa()));
    QVERIFY(products()->deactivateProduct(gaseosa().code));
    QCOMPARE(model->rowCount(), 0);                             // the list shows active ones
    QVERIFY(products()->product(gaseosa().code).has_value());   // still readable
    QVERIFY(!products()->product(gaseosa().code)->active);
    QVERIFY(products()->saveProduct(gaseosa()));                // saving again reactivates
    QCOMPARE(model->rowCount(), 1);
    QVERIFY(!products()->deactivateProduct(QStringLiteral("nope")));
    QCOMPARE(products()->lastError(), QString::fromUtf8("El artículo nope no existe."));
    delete model;
}

void tst_ProductService::brandsIgnoreCase()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(products()->saveProduct(gaseosa()));
    const qint64 id = products()->product(gaseosa().code)->brandId;
    QVERIFY(id != 0);
    QCOMPARE(products()->addBrand(QStringLiteral("coca cola")), id);   // no duplicate
    QCOMPARE(products()->brands().size(), 1);
}

void tst_ProductService::removedBrandStaysOnProducts()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(products()->saveProduct(gaseosa()));
    QVERIFY(products()->removeBrand(products()->product(gaseosa().code)->brandId));
    QVERIFY(products()->brands().isEmpty());                    // not offered any more
    QCOMPARE(products()->product(gaseosa().code)->brandName, QStringLiteral("COCA COLA"));
}

void tst_ProductService::importIsAllOrNothing()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    Product ok; ok.code = QStringLiteral("1001"); ok.description = QStringLiteral("PAN"); ok.price = 3000;
    Product broken; broken.description = QStringLiteral("SIN CODIGO");
    int saved = -1;
    QVERIFY(!products()->importProducts({ ok, broken }, &saved));
    QCOMPARE(saved, 0);
    QVERIFY(!products()->product(QStringLiteral("1001")).has_value());   // rolled back
    QVERIFY(products()->lastError().startsWith(QString::fromUtf8("Artículo")));

    QVERIFY(products()->importProducts({ ok }, &saved));
    QCOMPARE(saved, 1);
    QVERIFY(products()->product(QStringLiteral("1001")).has_value());
}

namespace {
int g_databaseWarnings = 0;
QtMessageHandler g_previousHandler = nullptr;
void countDatabaseWarnings(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (type >= QtWarningMsg && context.category && qstrcmp(context.category, "aguara.db") == 0)
        ++g_databaseWarnings;
    g_previousHandler(type, context, message);
}
} // namespace

/// A normal save must not trip any error path (regression: on SQLite the
/// service used to send PostgreSQL's NOTIFY, failing quietly every time).
void tst_ProductService::savingLogsNoDatabaseWarnings()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    g_databaseWarnings = 0;
    g_previousHandler = qInstallMessageHandler(countDatabaseWarnings);
    const bool saved = products()->saveProduct(gaseosa());
    const bool deactivated = products()->deactivateProduct(gaseosa().code);
    qInstallMessageHandler(g_previousHandler);
    QVERIFY(saved && deactivated);
    QCOMPARE(g_databaseWarnings, 0);
}

QTEST_GUILESS_MAIN(tst_ProductService)
#include "tst_productservice.moc"
