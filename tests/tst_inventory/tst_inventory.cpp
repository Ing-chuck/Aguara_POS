// Stock as a ledger, purchases, counts, barcodes and prices, on SQLite and
// PostgreSQL.
#include "testsupport.h"

#include <barcode.h>

#include <QtTest>

using TestSupport::CoreFixture;

class tst_Inventory : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase_data() { TestSupport::addBackendRows(); }
    void cleanup() { m_core.close(); }

    void newArticleOpensTheKardex();
    void stockIsAlwaysTheSumOfItsMovements();
    void adjustmentNeedsAReason();
    void stockMayGoNegative();
    void savingKeepsTheStockUnlessItChanged();
    void renameKeepsTheHistory();
    void decimalsAndUnits();

    void suppliers();
    void purchaseUpdatesEverything();
    void purchaseCanKeepCostsAndPrices();
    void purchaseIsAllOrNothing();
    void sameInvoiceTwiceIsRefused();
    void sameArticleTwiceOnOneInvoice();

    void countAdjustsOnlyWhatDiffers();
    void countSheetIncludesSubcategories();
    void countKeepsSalesMadeWhileCounting();

    void nestedCategories();
    void categoryCantGoInsideItself();
    void alternateBarcodes();
    void barcodeCantBelongToTwoArticles();
    void internalCodesAreNeverRepeated();
    void scaleCodes();

    void priceHistory();
    void bulkPriceChange();
    void bulkPriceFilters();
    void lowStock();

private:
    bool add(const QString &code, Quantity stock = 0, Money price = 10000, Money cost = 7000, qint64 category = 0);
    Quantity movementsSum(const QString &code) const;
    qint64 supplier(const QString &name = QStringLiteral("DISTRIBUIDORA"));
    InventoryService *inventory() { return m_core.inventory.get(); }
    ProductService *products() { return m_core.products.get(); }

    QTemporaryDir m_tmp;
    CoreFixture m_core;
};

bool tst_Inventory::add(const QString &code, Quantity stock, Money price, Money cost, qint64 category)
{
    Product p;
    p.code = code;
    p.description = QStringLiteral("ARTICULO ") + code;
    p.stock = stock;
    p.price = price;
    p.cost = cost;
    p.categoryId = category;
    return products()->saveProduct(p);
}

Quantity tst_Inventory::movementsSum(const QString &code) const
{
    Quantity sum = 0;
    for (const StockMovement &m : m_core.inventory->movements(code, 10000))
        sum += m.quantity;
    return sum;
}

qint64 tst_Inventory::supplier(const QString &name)
{
    Supplier s;
    s.name = name;
    return inventory()->saveSupplier(s);
}

void tst_Inventory::newArticleOpensTheKardex()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("1"), AguaraQuantity::whole(10)));
    const QList<StockMovement> moves = inventory()->movements(QStringLiteral("1"));
    QCOMPARE(moves.size(), 1);
    QCOMPARE(moves.first().type, QLatin1String(StockMovementType::Initial));
    QCOMPARE(moves.first().quantity, AguaraQuantity::whole(10));
    QCOMPARE(moves.first().balance, AguaraQuantity::whole(10));

    QVERIFY(add(QStringLiteral("2")));                      // nothing in stock: nothing to record
    QVERIFY(inventory()->movements(QStringLiteral("2")).isEmpty());
}

void tst_Inventory::stockIsAlwaysTheSumOfItsMovements()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    const QString code = QStringLiteral("1");
    QVERIFY(add(code, AguaraQuantity::whole(10)));
    QVERIFY(inventory()->adjustStock(code, -AguaraQuantity::whole(3), QStringLiteral("Rotura")));

    Purchase purchase;
    purchase.supplierId = supplier();
    purchase.lines = { { code, QString(), AguaraQuantity::whole(5), 8000, 0 } };
    QVERIFY2(inventory()->savePurchase(purchase), qPrintable(inventory()->lastError()));

    QList<CountLine> sheet = inventory()->countSheet(0);
    sheet[0].counted = AguaraQuantity::whole(11);
    sheet[0].wasCounted = true;
    QVERIFY(inventory()->applyCount(sheet, QString()));

    Product edited = *products()->product(code);            // through the article form or an import
    edited.stock = AguaraQuantity::whole(20);
    QVERIFY(products()->saveProduct(edited, code));

    const Quantity stock = products()->product(code)->stock;
    QCOMPARE(stock, AguaraQuantity::whole(20));
    QCOMPARE(movementsSum(code), stock);

    // and each movement's balance follows from the previous one
    const QList<StockMovement> moves = inventory()->movements(code);
    QCOMPARE(moves.size(), 5);
    QCOMPARE(moves.first().balance, stock);
    for (int i = 0; i + 1 < moves.size(); ++i)
        QCOMPARE(moves.at(i).balance, moves.at(i + 1).balance + moves.at(i).quantity);
}

void tst_Inventory::adjustmentNeedsAReason()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("1"), AguaraQuantity::whole(10)));
    QVERIFY(!inventory()->adjustStock(QStringLiteral("1"), AguaraQuantity::One, QStringLiteral("  ")));
    QVERIFY(inventory()->lastError().contains(QStringLiteral("motivo")));
    QVERIFY(!inventory()->adjustStock(QStringLiteral("1"), 0, QStringLiteral("x")));
    QVERIFY(!inventory()->adjustStock(QStringLiteral("nope"), AguaraQuantity::One, QStringLiteral("x")));
    QCOMPARE(products()->product(QStringLiteral("1"))->stock, AguaraQuantity::whole(10));
}

void tst_Inventory::stockMayGoNegative()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("1"), AguaraQuantity::whole(1)));
    QVERIFY(inventory()->adjustStock(QStringLiteral("1"), -AguaraQuantity::whole(3), QStringLiteral("Venta sin cargar")));
    QCOMPARE(products()->product(QStringLiteral("1"))->stock, -AguaraQuantity::whole(2));
    QCOMPARE(inventory()->movements(QStringLiteral("1")).first().balance, -AguaraQuantity::whole(2));
}

void tst_Inventory::savingKeepsTheStockUnlessItChanged()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("1"), AguaraQuantity::whole(5)));
    Product p = *products()->product(QStringLiteral("1"));
    p.description = QStringLiteral("OTRO NOMBRE");
    QVERIFY(products()->saveProduct(p, p.code));
    QCOMPARE(inventory()->movements(p.code).size(), 1);     // no movement for a rename of the text
}

void tst_Inventory::renameKeepsTheHistory()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("1"), AguaraQuantity::whole(5)));
    QVERIFY(inventory()->addBarcode(QStringLiteral("1"), QStringLiteral("999")));
    Product p = *products()->product(QStringLiteral("1"));
    p.code = QStringLiteral("2");
    QVERIFY2(products()->saveProduct(p, QStringLiteral("1")), qPrintable(products()->lastError()));
    QCOMPARE(inventory()->movements(QStringLiteral("2")).size(), 1);
    QCOMPARE(inventory()->barcodes(QStringLiteral("2")), QStringList{ QStringLiteral("999") });
}

void tst_Inventory::decimalsAndUnits()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    const QList<StockUnit> units = products()->units();
    QCOMPARE(units.first().name, QStringLiteral("Unidad"));
    QCOMPARE(units.at(1).abbreviation, QStringLiteral("kg"));
    QCOMPARE(units.at(1).decimals, 3);

    QAbstractItemModel *model = products()->createProductTableModel(this);
    Product p;
    p.code = QStringLiteral("2012345");
    p.description = QStringLiteral("QUESO");
    p.unit = units.at(1);
    p.stock = 2750;                                         // 2,750 kg
    QVERIFY(products()->saveProduct(p));
    const auto got = products()->product(p.code);
    QCOMPARE(got->stock, Quantity(2750));
    QCOMPARE(got->unit.abbreviation, QStringLiteral("kg"));
    QCOMPARE(model->index(0, int(ProductColumn::Stock)).data().toString(), QStringLiteral("2,750 kg"));
    QCOMPARE(model->columnCount(), int(ProductColumn::Count));
    delete model;
}

void tst_Inventory::suppliers()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    Supplier s;
    s.name = QStringLiteral("  ");
    QCOMPARE(inventory()->saveSupplier(s), qint64(0));
    s.name = QStringLiteral("DISTRIBUIDORA");
    s.phone = QStringLiteral("0981 123456");
    s.id = inventory()->saveSupplier(s);
    QVERIFY(s.id);
    s.contact = QStringLiteral("Ana");
    QCOMPARE(inventory()->saveSupplier(s), s.id);
    QCOMPARE(inventory()->suppliers().size(), 1);
    QCOMPARE(inventory()->suppliers().first().contact, QStringLiteral("Ana"));

    Supplier twin;
    twin.name = QStringLiteral(" distribuidora ");
    QCOMPARE(inventory()->saveSupplier(twin), qint64(0));
    QVERIFY(inventory()->lastError().contains(QStringLiteral("Ya existe")));
    Supplier bad;
    bad.name = QStringLiteral("OTRO");
    bad.phone = QStringLiteral("no tiene");
    QCOMPARE(inventory()->saveSupplier(bad), qint64(0));
    bad.phone.clear();
    bad.email = QStringLiteral("otro");
    QCOMPARE(inventory()->saveSupplier(bad), qint64(0));
    bad.email.clear();
    bad.web = QStringLiteral("otro");
    QCOMPARE(inventory()->saveSupplier(bad), qint64(0));
    bad.web = QStringLiteral("www.otro.com.py");
    QVERIFY(inventory()->saveSupplier(bad) != 0);
    QVERIFY(inventory()->deactivateSupplier(inventory()->suppliers().last().id));

    QVERIFY(inventory()->deactivateSupplier(s.id));
    QVERIFY(inventory()->suppliers().isEmpty());
    QCOMPARE(inventory()->suppliers(true).size(), 2);
}

void tst_Inventory::purchaseUpdatesEverything()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("1"), AguaraQuantity::whole(10), 10000, 7000));
    QVERIFY(add(QStringLiteral("2"), 0, 5000, 3000));

    Purchase purchase;
    purchase.supplierId = supplier();
    purchase.invoice = QStringLiteral("001-001-0000123");
    purchase.date = QDate::currentDate();
    purchase.lines = { { QStringLiteral("1"), QString(), AguaraQuantity::whole(6), 7500, 11000 },
                       { QStringLiteral("2"), QString(), 2500, 3200, 0 } };     // 2,5 units
    const qint64 id = inventory()->savePurchase(purchase);
    QVERIFY2(id, qPrintable(inventory()->lastError()));

    const auto one = products()->product(QStringLiteral("1"));
    QCOMPARE(one->stock, AguaraQuantity::whole(16));
    QCOMPARE(one->cost, Money(7500));
    QCOMPARE(one->price, Money(11000));
    QCOMPARE(one->purchased, QDate::currentDate());
    const auto two = products()->product(QStringLiteral("2"));
    QCOMPARE(two->stock, Quantity(2500));
    QCOMPARE(two->cost, Money(3200));
    QCOMPARE(two->price, Money(5000));                      // no new price: kept

    const StockMovement move = inventory()->movements(QStringLiteral("1")).first();
    QCOMPARE(move.type, QLatin1String(StockMovementType::Purchase));
    QVERIFY(move.document.contains(QStringLiteral("001-001-0000123")));
    QCOMPARE(move.unitCost, Money(7500));

    const auto saved = inventory()->purchase(id);
    QVERIFY(saved.has_value());
    QCOMPARE(saved->total, Money(45000 + 8000));
    QCOMPARE(saved->lines.size(), 2);
    QCOMPARE(saved->lines.first().description, QStringLiteral("ARTICULO 1"));
    QCOMPARE(saved->supplierName, QStringLiteral("DISTRIBUIDORA"));
    QCOMPARE(inventory()->purchases(QDate::currentDate(), QDate::currentDate()).size(), 1);
    QVERIFY(inventory()->purchases(QDate(2000, 1, 1), QDate(2000, 1, 2)).isEmpty());
}

void tst_Inventory::purchaseCanKeepCostsAndPrices()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("1"), 0, 10000, 7000));
    Purchase purchase;
    purchase.supplierId = supplier();
    purchase.updateCosts = false;
    purchase.lines = { { QStringLiteral("1"), QString(), AguaraQuantity::whole(1), 9000, 0 } };
    QVERIFY(inventory()->savePurchase(purchase));
    QCOMPARE(products()->product(QStringLiteral("1"))->cost, Money(7000));
    QCOMPARE(products()->product(QStringLiteral("1"))->stock, AguaraQuantity::whole(1));
}

void tst_Inventory::purchaseIsAllOrNothing()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("1"), 0));
    Purchase purchase;
    purchase.lines = { { QStringLiteral("1"), QString(), AguaraQuantity::whole(1), 100, 0 } };
    QVERIFY(!inventory()->savePurchase(purchase));          // no supplier
    purchase.supplierId = supplier();

    purchase.lines << PurchaseLine{ QStringLiteral("nope"), QString(), AguaraQuantity::whole(1), 100, 0 };
    QVERIFY(!inventory()->savePurchase(purchase));
    purchase.lines.last() = { QStringLiteral("1"), QString(), 0, 100, 0 };
    QVERIFY(!inventory()->savePurchase(purchase));          // no quantity
    purchase.lines.clear();
    QVERIFY(!inventory()->savePurchase(purchase));          // no lines

    QCOMPARE(products()->product(QStringLiteral("1"))->stock, Quantity(0));
    QVERIFY(inventory()->movements(QStringLiteral("1")).isEmpty());
    QVERIFY(inventory()->purchases(QDate(2000, 1, 1), QDate::currentDate()).isEmpty());
}

void tst_Inventory::sameInvoiceTwiceIsRefused()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("1"), 0));
    Purchase purchase;
    purchase.supplierId = supplier();
    purchase.invoice = QStringLiteral("123");
    purchase.lines = { { QStringLiteral("1"), QString(), AguaraQuantity::whole(1), 100, 0 } };
    QVERIFY(inventory()->savePurchase(purchase));
    QVERIFY(!inventory()->savePurchase(purchase));
    QVERIFY(inventory()->lastError().contains(QStringLiteral("ya está cargada")));
    QCOMPARE(products()->product(QStringLiteral("1"))->stock, AguaraQuantity::whole(1));

    purchase.supplierId = supplier(QStringLiteral("OTRO"));  // another supplier's number
    QVERIFY(inventory()->savePurchase(purchase));
}

void tst_Inventory::sameArticleTwiceOnOneInvoice()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("1"), 0, 1000, 500));
    Purchase purchase;
    purchase.supplierId = supplier();
    purchase.lines = { { QStringLiteral("1"), QString(), AguaraQuantity::whole(1), 600, 1200 },
                       { QStringLiteral("1"), QString(), AguaraQuantity::whole(2), 700, 0 } };
    QVERIFY(inventory()->savePurchase(purchase));
    const auto p = products()->product(QStringLiteral("1"));
    QCOMPARE(p->stock, AguaraQuantity::whole(3));
    QCOMPARE(p->cost, Money(700));
    QCOMPARE(p->price, Money(1200));                        // the second line didn't undo the first
    QCOMPARE(movementsSum(QStringLiteral("1")), p->stock);
}

void tst_Inventory::countAdjustsOnlyWhatDiffers()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("1"), AguaraQuantity::whole(10)));
    QVERIFY(add(QStringLiteral("2"), AguaraQuantity::whole(10)));
    QVERIFY(add(QStringLiteral("3"), AguaraQuantity::whole(10)));

    QList<CountLine> sheet = inventory()->countSheet(0);
    QCOMPARE(sheet.size(), 3);
    sheet[0].counted = AguaraQuantity::whole(8);            // short by two
    sheet[0].wasCounted = true;
    sheet[1].counted = AguaraQuantity::whole(10);           // right
    sheet[1].wasCounted = true;
    // the third wasn't counted

    int adjusted = -1;
    QVERIFY(inventory()->applyCount(sheet, QStringLiteral("Recuento de prueba"), &adjusted));
    QCOMPARE(adjusted, 1);
    QCOMPARE(products()->product(sheet[0].code)->stock, AguaraQuantity::whole(8));
    QCOMPARE(products()->product(sheet[1].code)->stock, AguaraQuantity::whole(10));
    QCOMPARE(products()->product(sheet[2].code)->stock, AguaraQuantity::whole(10));
    const StockMovement move = inventory()->movements(sheet[0].code).first();
    QCOMPARE(move.type, QLatin1String(StockMovementType::Count));
    QCOMPARE(move.quantity, -AguaraQuantity::whole(2));
    QCOMPARE(move.reason, QStringLiteral("Recuento de prueba"));
}

void tst_Inventory::countSheetIncludesSubcategories()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    const qint64 food = products()->addCategory(QStringLiteral("ALMACEN"));
    const qint64 dairy = products()->addCategory(QStringLiteral("LACTEOS"), food);
    const qint64 other = products()->addCategory(QStringLiteral("LIMPIEZA"));
    QVERIFY(add(QStringLiteral("1"), 0, 100, 50, food));
    QVERIFY(add(QStringLiteral("2"), 0, 100, 50, dairy));
    QVERIFY(add(QStringLiteral("3"), 0, 100, 50, other));

    QCOMPARE(inventory()->countSheet(food).size(), 2);
    QCOMPARE(inventory()->countSheet(dairy).size(), 1);
    QCOMPARE(inventory()->countSheet(0).size(), 3);
}

void tst_Inventory::countKeepsSalesMadeWhileCounting()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("1"), AguaraQuantity::whole(10)));
    QList<CountLine> sheet = inventory()->countSheet(0);

    // two go out while the shelf is being counted
    QVERIFY(inventory()->adjustStock(QStringLiteral("1"), -AguaraQuantity::whole(2), QStringLiteral("Venta")));

    sheet[0].counted = AguaraQuantity::whole(9);            // one missing, by the sheet
    sheet[0].wasCounted = true;
    QVERIFY(inventory()->applyCount(sheet, QString()));
    QCOMPARE(products()->product(QStringLiteral("1"))->stock, AguaraQuantity::whole(7));
}

void tst_Inventory::nestedCategories()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    const qint64 food = products()->addCategory(QStringLiteral("ALMACEN"));
    const qint64 dairy = products()->addCategory(QStringLiteral("LACTEOS"), food);
    const qint64 cheese = products()->addCategory(QStringLiteral("QUESOS"), dairy);
    QVERIFY(food && dairy && cheese);

    const QList<NamedItem> all = products()->categories();
    QCOMPARE(all.size(), 3);
    QCOMPARE(all.at(2).path, QStringLiteral("ALMACEN > LACTEOS > QUESOS"));
    QCOMPARE(all.at(2).parentId, dairy);
    QCOMPARE(all.at(0).path, QStringLiteral("ALMACEN"));
}

void tst_Inventory::categoryCantGoInsideItself()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    const qint64 a = products()->addCategory(QStringLiteral("A"));
    const qint64 b = products()->addCategory(QStringLiteral("B"), a);
    QVERIFY(!products()->setCategoryParent(a, a));
    QVERIFY(!products()->setCategoryParent(a, b));          // a loop through its child
    QVERIFY(products()->lastError().contains(QStringLiteral("dentro de sí mismo")));
    QVERIFY(products()->setCategoryParent(b, 0));
    QCOMPARE(products()->categories().at(1).path, QStringLiteral("B"));
}

void tst_Inventory::alternateBarcodes()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("1")));
    QVERIFY(inventory()->addBarcode(QStringLiteral("1"), QStringLiteral("7840001")));
    QVERIFY(inventory()->addBarcode(QStringLiteral("1"), QStringLiteral("7840002")));
    QCOMPARE(inventory()->barcodes(QStringLiteral("1")).size(), 2);

    const ScanResult scan = inventory()->resolveScan(QStringLiteral(" 7840002 "));
    QVERIFY(scan.found);
    QCOMPARE(scan.product.code, QStringLiteral("1"));
    QCOMPARE(scan.quantity, AguaraQuantity::One);
    QCOMPARE(scan.price, Money(10000));

    QVERIFY(inventory()->removeBarcode(QStringLiteral("7840002")));
    QVERIFY(!inventory()->resolveScan(QStringLiteral("7840002")).found);
    QVERIFY(inventory()->resolveScan(QStringLiteral("1")).found);

    QVERIFY(products()->deactivateProduct(QStringLiteral("1")));    // not for sale any more
    QVERIFY(!inventory()->resolveScan(QStringLiteral("1")).found);
    QVERIFY(!inventory()->resolveScan(QStringLiteral("7840001")).found);
}

void tst_Inventory::internalCodesAreNeverRepeated()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    const QString first = m_core.products->newInternalCode();
    QCOMPARE(first, QStringLiteral("299") + QStringLiteral("000000001")
                    + QString::number(AguaraBarcode::ean13CheckDigit(QStringLiteral("299000000001"))));
    QVERIFY(AguaraBarcode::isValidEan13(first));
    QCOMPARE(m_core.products->newInternalCode(), first);            // nothing took it yet

    Product p;
    p.code = first;
    p.description = QStringLiteral("SIN CODIGO DE BARRAS");
    p.price = 1000;
    p.vatRate = 10;
    QVERIFY(m_core.products->saveProduct(p));

    const QString second = m_core.products->newInternalCode();
    QVERIFY(second != first);
    QVERIFY(inventory()->addBarcode(first, second));                // used as a barcode counts too
    const QString third = m_core.products->newInternalCode();
    QVERIFY(third != second && third != first);

    QVERIFY(m_core.products->deactivateProduct(first));             // an old article keeps its code
    QVERIFY(m_core.products->newInternalCode() == third);

    // a scale label's range: the code itself is what's found, with no weight read from it
    QVERIFY(m_core.products->saveProduct(p));
    const ScanResult scan = inventory()->resolveScan(first);
    QVERIFY(scan.found);
    QCOMPARE(scan.product.code, first);
    QCOMPARE(scan.quantity, AguaraQuantity::One);
}

void tst_Inventory::barcodeCantBelongToTwoArticles()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("1")));
    QVERIFY(add(QStringLiteral("2")));
    QVERIFY(inventory()->addBarcode(QStringLiteral("1"), QStringLiteral("999")));
    QVERIFY(!inventory()->addBarcode(QStringLiteral("2"), QStringLiteral("999")));
    QVERIFY(!inventory()->addBarcode(QStringLiteral("2"), QStringLiteral("1")));     // another's main code
    QVERIFY(!inventory()->addBarcode(QStringLiteral("2"), QStringLiteral(" ")));
    QVERIFY(!add(QStringLiteral("999")));                   // and a new article can't take it
    QVERIFY(products()->lastError().contains(QStringLiteral("código de barras")));
}

void tst_Inventory::scaleCodes()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    Product cheese;
    cheese.code = QStringLiteral("2012345");
    cheese.description = QStringLiteral("QUESO");
    cheese.price = 40000;                                   // per kg
    cheese.unit.id = 2;
    QVERIFY(products()->saveProduct(cheese));
    Product ham = cheese;
    ham.code = QStringLiteral("2854321");
    ham.description = QStringLiteral("JAMON");
    ham.price = 50000;
    QVERIFY(products()->saveProduct(ham));

    const auto withCheck = [](const QString &first12) {
        return first12 + QString::number(AguaraBarcode::ean13CheckDigit(first12));
    };

    ScanResult weighed = inventory()->resolveScan(withCheck(QStringLiteral("201234501250")));   // 1250 g
    QVERIFY(weighed.found);
    QCOMPARE(weighed.product.code, cheese.code);
    QCOMPARE(weighed.quantity, Quantity(1250));
    QCOMPARE(weighed.price, Money(40000));

    ScanResult priced = inventory()->resolveScan(withCheck(QStringLiteral("285432125000")));    // Gs. 25.000
    QVERIFY(priced.found);
    QCOMPARE(priced.product.code, ham.code);
    QCOMPARE(priced.price, Money(25000));
    QCOMPARE(priced.quantity, Quantity(500));               // half a kilo

    QString damaged = withCheck(QStringLiteral("201234501250"));
    damaged[12] = damaged.at(12) == QLatin1Char('9') ? QLatin1Char('0') : QChar(damaged.at(12).unicode() + 1);
    QVERIFY(!inventory()->resolveScan(damaged).found);      // a misread code isn't a sale
    QVERIFY(!inventory()->resolveScan(withCheck(QStringLiteral("209999901250"))).found);        // unknown article
}

void tst_Inventory::priceHistory()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("1"), 0, 10000, 7000));
    QVERIFY(inventory()->priceHistory(QStringLiteral("1")).isEmpty());  // a first price isn't a change

    Product p = *products()->product(QStringLiteral("1"));
    p.price = 11000;
    QVERIFY(products()->saveProduct(p, p.code));
    p.description = QStringLiteral("OTRO");
    QVERIFY(products()->saveProduct(p, p.code));            // neither price nor cost

    Purchase purchase;
    purchase.supplierId = supplier();
    purchase.lines = { { p.code, QString(), AguaraQuantity::One, 8000, 0 } };
    QVERIFY(inventory()->savePurchase(purchase));

    const QList<PriceChange> history = inventory()->priceHistory(p.code);
    QCOMPARE(history.size(), 2);
    QCOMPARE(history.at(0).oldCost, Money(7000));
    QCOMPARE(history.at(0).newCost, Money(8000));
    QCOMPARE(history.at(1).oldPrice, Money(10000));
    QCOMPARE(history.at(1).newPrice, Money(11000));
}

void tst_Inventory::bulkPriceChange()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("1"), 0, 10000));
    QVERIFY(add(QStringLiteral("2"), 0, 12345));
    QVERIFY(add(QStringLiteral("3"), 0, 0));                // no price: left alone

    const QList<PricePreview> preview = inventory()->previewPriceChange(PriceFilter(), 1000, 100);
    QCOMPARE(preview.size(), 2);
    QCOMPARE(preview.at(0).newPrice, Money(11000));
    QCOMPARE(preview.at(1).newPrice, Money(13600));         // 13.580 up to the next 100
    QCOMPARE(products()->product(QStringLiteral("1"))->price, Money(10000));    // a preview changes nothing

    int changed = 0;
    QVERIFY(inventory()->applyPriceChange(PriceFilter(), 1000, 100, &changed));
    QCOMPARE(changed, 2);
    QCOMPARE(products()->product(QStringLiteral("2"))->price, Money(13600));
    QCOMPARE(inventory()->priceHistory(QStringLiteral("2")).size(), 1);

    QVERIFY(inventory()->applyPriceChange(PriceFilter(), -5000, 0, &changed));  // down too
    QCOMPARE(products()->product(QStringLiteral("1"))->price, Money(5500));
}

void tst_Inventory::bulkPriceFilters()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    const qint64 food = products()->addCategory(QStringLiteral("ALMACEN"));
    const qint64 dairy = products()->addCategory(QStringLiteral("LACTEOS"), food);
    QVERIFY(add(QStringLiteral("1"), 0, 1000, 500, food));
    QVERIFY(add(QStringLiteral("2"), 0, 1000, 500, dairy));
    QVERIFY(add(QStringLiteral("3"), 0, 1000, 500));

    PriceFilter byCategory;
    byCategory.categoryId = food;
    QCOMPARE(inventory()->previewPriceChange(byCategory, 1000, 0).size(), 2);   // and what's inside

    Purchase purchase;
    purchase.supplierId = supplier();
    purchase.lines = { { QStringLiteral("3"), QString(), AguaraQuantity::One, 500, 0 } };
    QVERIFY(inventory()->savePurchase(purchase));
    PriceFilter bySupplier;
    bySupplier.supplierId = purchase.supplierId;
    const QList<PricePreview> list = inventory()->previewPriceChange(bySupplier, 1000, 0);
    QCOMPARE(list.size(), 1);
    QCOMPARE(list.first().code, QStringLiteral("3"));

    Product branded = *products()->product(QStringLiteral("1"));
    branded.brandName = QStringLiteral("KURUPI");
    QVERIFY(products()->saveProduct(branded, branded.code));
    PriceFilter byBrand;
    byBrand.brandId = products()->product(QStringLiteral("1"))->brandId;
    QCOMPARE(inventory()->previewPriceChange(byBrand, 1000, 0).size(), 1);
}

void tst_Inventory::lowStock()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    QVERIFY(add(QStringLiteral("1"), AguaraQuantity::whole(2)));
    Product a = *products()->product(QStringLiteral("1"));
    a.stockMin = AguaraQuantity::whole(5);
    QVERIFY(products()->saveProduct(a, a.code));
    QVERIFY(add(QStringLiteral("2"), AguaraQuantity::whole(50)));   // no minimum

    const QList<Product> low = inventory()->lowStock();
    QCOMPARE(low.size(), 1);
    QCOMPARE(low.first().code, QStringLiteral("1"));

    QVERIFY(inventory()->adjustStock(a.code, AguaraQuantity::whole(10), QStringLiteral("x")));
    QVERIFY(inventory()->lowStock().isEmpty());
}

QTEST_GUILESS_MAIN(tst_Inventory)
#include "tst_inventory.moc"
