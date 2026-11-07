// Promotions: which articles and days they reach, what they take off a line, and
// that a sale keeps what it was given, on SQLite and PostgreSQL. The till and the
// page are in tst_modules.
#include "sqluserservice.h"
#include "testsupport.h"

#include <QTemporaryDir>
#include <QtTest>

using TestSupport::CoreFixture;
using TestSupport::inCash;

namespace {

CashCount worth(Money amount)
{
    DenominationCount coin;
    coin.denomination = 50;
    coin.count = int(amount / 50);
    return { coin };
}

SaleLine lineOf(Money price, Quantity quantity = AguaraQuantity::One)
{
    SaleLine line;
    line.code = QStringLiteral("1");
    line.description = QStringLiteral("ARTICULO 1");
    line.unit = QStringLiteral("un");
    line.unitPrice = price;
    line.quantity = quantity;
    return line;
}

Promotion ofKind(PromotionKind kind, Money value)
{
    Promotion p;
    p.name = QStringLiteral("PROMO");
    p.kind = kind;
    p.value = value;
    p.from = QDate::currentDate();
    p.to = p.from.addDays(10);
    return p;
}

PromotionRule article(const QString &code, bool excluded = false)
{
    PromotionRule rule;
    rule.code = code;
    rule.excluded = excluded;
    return rule;
}

PromotionRule category(qint64 id, bool excluded = false)
{
    PromotionRule rule;
    rule.target = PromotionRule::Category;
    rule.id = id;
    rule.excluded = excluded;
    return rule;
}

PromotionRule brand(qint64 id, bool excluded = false)
{
    PromotionRule rule;
    rule.target = PromotionRule::Brand;
    rule.id = id;
    rule.excluded = excluded;
    return rule;
}

QStringList codes(const QList<Product> &articles)
{
    QStringList list;
    for (const Product &p : articles)
        list << p.code;
    list.sort();
    return list;
}

} // namespace

class tst_Promotions : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase_data() { TestSupport::addBackendRows(); }
    void cleanup();

    void percentRounds();
    void amountOffCountsTheUnits();
    void aFixedPrice();
    void aFixedPriceAboveThePriceGivesNothing();
    void buyAndPayCountsWholeSets();
    void theMinimumIsOnTheLine();
    void itNeverPassesTheLine();
    void aScaleLabelIsDiscountedOnItsPrice();
    void theBestDealWins();
    void equalDealsGoToTheOldest();

    void datesAreInclusive();
    void weekdaysLimitIt();
    void takenDownItDoesntRun();

    void selectionByArticleCategoryAndBrandAddsUp();
    void categoryAndBrandAtOnce();
    void invertedTakesTheOthers();
    void excludedHasTheLastWord();
    void everythingLessWhatIsExcluded();

    void itComesBackAsSaved();
    void whatItRefuses();
    void aChangeReplacesTheRules();
    void aSubcategoryIsInItsParent();
    void offersAreOnlyWhatRunsToday();
    void anArticleOutOfUseIsntListed();
    void aNewCodeKeepsTheRule();

    void theSaleKeepsWhatItWasGiven();
    void aPromotionNeedsNoRoomInTheManualDiscount();
    void itSurvivesTheQueue();
    void aDiscountThatPassesTheLineIsRefused();
    void aReturnPaysWhatWasCharged();
    void theReportIsNetOfReturns();

private:
    void fill();
    bool openTill();
    qint64 sell(const SaleDraft &draft);

    QTemporaryDir m_tmp;
    CoreFixture m_core;
    qint64 m_dairy = 0, m_milk = 0, m_drinks = 0;
    qint64 m_cow = 0, m_cola = 0;
};

void tst_Promotions::cleanup()
{
    m_core.close();
}

/// LACTEOS > LECHES, BEBIDAS; brands VACA and COLA.
///     1 LECHE (LECHES, VACA)   2 YOGUR (LACTEOS, VACA)
///     3 QUESO (LACTEOS, COLA)  4 GASEOSA (BEBIDAS, COLA)
void tst_Promotions::fill()
{
    m_dairy = m_core.products->addCategory(QStringLiteral("LACTEOS"));
    m_milk = m_core.products->addCategory(QStringLiteral("LECHES"), m_dairy);
    m_drinks = m_core.products->addCategory(QStringLiteral("BEBIDAS"));
    m_cow = m_core.products->addBrand(QStringLiteral("VACA"));
    m_cola = m_core.products->addBrand(QStringLiteral("COLA"));
    const auto add = [this](const char *code, const char *name, qint64 rubro, qint64 marca) {
        Product p;
        p.code = QLatin1String(code);
        p.description = QLatin1String(name);
        p.price = 10000;
        p.cost = 5000;
        p.categoryId = rubro;
        p.brandId = marca;
        p.stock = AguaraQuantity::whole(50);
        QVERIFY(m_core.products->saveProduct(p));
    };
    add("1", "LECHE", m_milk, m_cow);
    add("2", "YOGUR", m_dairy, m_cow);
    add("3", "QUESO", m_dairy, m_cola);
    add("4", "GASEOSA", m_drinks, m_cola);
}

bool tst_Promotions::openTill()
{
    return m_core.cash->openSession(worth(100000));
}

qint64 tst_Promotions::sell(const SaleDraft &draft)
{
    return m_core.sales->checkout(draft, { inCash(AguaraSale::cashDue(AguaraSale::totals(draft).net, 0)) });
}

// --- what it takes off -------------------------------------------------------

void tst_Promotions::percentRounds()
{
    QCOMPARE(AguaraPromotion::discount(ofKind(PromotionKind::Percent, 15), lineOf(3333)), Money(500));   // 499,95
    QCOMPARE(AguaraPromotion::discount(ofKind(PromotionKind::Percent, 100), lineOf(3333)), Money(3333));
}

void tst_Promotions::amountOffCountsTheUnits()
{
    const Promotion p = ofKind(PromotionKind::AmountOff, 500);
    QCOMPARE(AguaraPromotion::discount(p, lineOf(4000, AguaraQuantity::whole(3))), Money(1500));
    QCOMPARE(AguaraPromotion::discount(p, lineOf(4000, 1500)), Money(750));         // 1,5 kg
}

void tst_Promotions::aFixedPrice()
{
    QCOMPARE(AguaraPromotion::discount(ofKind(PromotionKind::FixedPrice, 9000), lineOf(11000, AguaraQuantity::whole(2))), Money(4000));
}

void tst_Promotions::aFixedPriceAboveThePriceGivesNothing()
{
    QCOMPARE(AguaraPromotion::discount(ofKind(PromotionKind::FixedPrice, 12000), lineOf(11000)), Money(0));
}

void tst_Promotions::buyAndPayCountsWholeSets()
{
    Promotion p = ofKind(PromotionKind::BuyPay, 0);
    p.buy = 3;
    p.pay = 2;
    QCOMPARE(AguaraPromotion::discount(p, lineOf(1000, AguaraQuantity::whole(2))), Money(0));
    QCOMPARE(AguaraPromotion::discount(p, lineOf(1000, AguaraQuantity::whole(3))), Money(1000));
    QCOMPARE(AguaraPromotion::discount(p, lineOf(1000, AguaraQuantity::whole(5))), Money(1000));
    QCOMPARE(AguaraPromotion::discount(p, lineOf(1000, AguaraQuantity::whole(7))), Money(2000));
}

void tst_Promotions::theMinimumIsOnTheLine()
{
    Promotion p = ofKind(PromotionKind::Percent, 10);
    p.minimum = AguaraQuantity::whole(6);
    QCOMPARE(AguaraPromotion::discount(p, lineOf(1000, AguaraQuantity::whole(5))), Money(0));
    QCOMPARE(AguaraPromotion::discount(p, lineOf(1000, AguaraQuantity::whole(6))), Money(600));
}

void tst_Promotions::itNeverPassesTheLine()
{
    QCOMPARE(AguaraPromotion::discount(ofKind(PromotionKind::AmountOff, 5000), lineOf(4000)), Money(4000));
}

void tst_Promotions::aScaleLabelIsDiscountedOnItsPrice()
{
    SaleLine label = lineOf(10000, 1254);
    label.fixedAmount = 12540;
    QCOMPARE(AguaraPromotion::discount(ofKind(PromotionKind::Percent, 10), label), Money(1254));
    QCOMPARE(AguaraPromotion::discount(ofKind(PromotionKind::FixedPrice, 8000), label), Money(2508));
}

void tst_Promotions::theBestDealWins()
{
    Promotion small = ofKind(PromotionKind::Percent, 5);
    small.id = 1;
    Promotion large = ofKind(PromotionKind::FixedPrice, 800);
    large.id = 2;
    Promotion needsMore = ofKind(PromotionKind::Percent, 90);
    needsMore.id = 3;
    needsMore.minimum = AguaraQuantity::whole(10);
    QCOMPARE(AguaraPromotion::best({ small, large, needsMore }, lineOf(1000)), 1);
    QCOMPARE(AguaraPromotion::best({ small, large, needsMore }, lineOf(1000, AguaraQuantity::whole(10))), 2);
    QCOMPARE(AguaraPromotion::best({ needsMore }, lineOf(1000)), -1);
    QCOMPARE(AguaraPromotion::best({}, lineOf(1000)), -1);
}

void tst_Promotions::equalDealsGoToTheOldest()
{
    Promotion newer = ofKind(PromotionKind::Percent, 10);
    newer.id = 9;
    Promotion older = ofKind(PromotionKind::AmountOff, 100);
    older.id = 2;
    QCOMPARE(AguaraPromotion::best({ newer, older }, lineOf(1000)), 1);
}

// --- when --------------------------------------------------------------------

void tst_Promotions::datesAreInclusive()
{
    Promotion p = ofKind(PromotionKind::Percent, 10);
    p.from = QDate(2026, 11, 2);
    p.to = QDate(2026, 11, 8);
    QVERIFY(!AguaraPromotion::runsOn(p, QDate(2026, 11, 1)));
    QVERIFY(AguaraPromotion::runsOn(p, QDate(2026, 11, 2)));
    QVERIFY(AguaraPromotion::runsOn(p, QDate(2026, 11, 8)));
    QVERIFY(!AguaraPromotion::runsOn(p, QDate(2026, 11, 9)));
}

void tst_Promotions::weekdaysLimitIt()
{
    Promotion p = ofKind(PromotionKind::Percent, 10);
    p.from = QDate(2026, 11, 2);        // a monday
    p.to = QDate(2026, 11, 8);
    p.weekdays = (1 << 1) | (1 << 6);   // tuesday and sunday
    QVERIFY(!AguaraPromotion::runsOn(p, QDate(2026, 11, 2)));
    QVERIFY(AguaraPromotion::runsOn(p, QDate(2026, 11, 3)));
    QVERIFY(!AguaraPromotion::runsOn(p, QDate(2026, 11, 4)));
    QVERIFY(AguaraPromotion::runsOn(p, QDate(2026, 11, 8)));
    QCOMPARE(AguaraPromotion::weekdayNames(p.weekdays), QStringLiteral("Mar, Dom"));
    QCOMPARE(AguaraPromotion::weekdayNames(AguaraPromotion::AllWeekdays), QStringLiteral("Todos los días"));
}

void tst_Promotions::takenDownItDoesntRun()
{
    Promotion p = ofKind(PromotionKind::Percent, 10);
    p.active = false;
    QVERIFY(!AguaraPromotion::runsOn(p, QDate::currentDate()));
}

// --- which articles ----------------------------------------------------------

void tst_Promotions::selectionByArticleCategoryAndBrandAddsUp()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    fill();
    Promotion p = ofKind(PromotionKind::Percent, 10);
    p.rules = { article(QStringLiteral("4")) };
    QCOMPARE(codes(m_core.promotions->articlesIn(p)), QStringList({ "4" }));
    p.rules = { category(m_dairy) };
    QCOMPARE(codes(m_core.promotions->articlesIn(p)), QStringList({ "1", "2", "3" }));
    p.rules = { brand(m_cola) };
    QCOMPARE(codes(m_core.promotions->articlesIn(p)), QStringList({ "3", "4" }));
    p.rules = { category(m_milk), brand(m_cola) };
    QCOMPARE(codes(m_core.promotions->articlesIn(p)), QStringList({ "1", "3", "4" }));
}

void tst_Promotions::categoryAndBrandAtOnce()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    fill();
    Promotion p = ofKind(PromotionKind::Percent, 10);
    p.categoryAndBrand = true;
    p.rules = { category(m_dairy), brand(m_cow) };
    QCOMPARE(codes(m_core.promotions->articlesIn(p)), QStringList({ "1", "2" }));
    p.rules << article(QStringLiteral("4"));                // an article is always in
    QCOMPARE(codes(m_core.promotions->articlesIn(p)), QStringList({ "1", "2", "4" }));
    p.rules = { category(m_drinks) };                       // nothing to cross it with
    QCOMPARE(codes(m_core.promotions->articlesIn(p)), QStringList({ "4" }));
}

void tst_Promotions::invertedTakesTheOthers()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    fill();
    Promotion p = ofKind(PromotionKind::Percent, 10);
    p.inverted = true;
    p.rules = { category(m_dairy) };
    QCOMPARE(codes(m_core.promotions->articlesIn(p)), QStringList({ "4" }));
    p.rules = { brand(m_cola), article(QStringLiteral("1")) };
    QCOMPARE(codes(m_core.promotions->articlesIn(p)), QStringList({ "2" }));
}

void tst_Promotions::excludedHasTheLastWord()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    fill();
    Promotion p = ofKind(PromotionKind::Percent, 10);
    p.rules = { category(m_dairy), article(QStringLiteral("2"), true) };
    QCOMPARE(codes(m_core.promotions->articlesIn(p)), QStringList({ "1", "3" }));
    p.rules = { category(m_dairy), brand(m_cola, true) };
    QCOMPARE(codes(m_core.promotions->articlesIn(p)), QStringList({ "1", "2" }));
    p.rules = { brand(m_cow), category(m_milk, true) };     // a subcategory too
    QCOMPARE(codes(m_core.promotions->articlesIn(p)), QStringList({ "2" }));

    p.inverted = true;                                      // turned around, it stays out
    p.rules = { article(QStringLiteral("1")), article(QStringLiteral("2"), true) };
    QCOMPARE(codes(m_core.promotions->articlesIn(p)), QStringList({ "3", "4" }));
}

void tst_Promotions::everythingLessWhatIsExcluded()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    fill();
    Promotion p = ofKind(PromotionKind::Percent, 10);
    p.everything = true;
    QCOMPARE(m_core.promotions->articlesIn(p).size(), 4);
    p.rules = { brand(m_cola, true) };
    QCOMPARE(codes(m_core.promotions->articlesIn(p)), QStringList({ "1", "2" }));
}

// --- saved -------------------------------------------------------------------

void tst_Promotions::itComesBackAsSaved()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    fill();
    Promotion p = ofKind(PromotionKind::BuyPay, 0);
    p.name = QStringLiteral("  TRES   POR DOS ");
    p.buy = 3;
    p.pay = 2;
    p.weekdays = 5;
    p.categoryAndBrand = true;
    p.rules = { category(m_dairy), brand(m_cow), article(QStringLiteral("2"), true) };
    const qint64 id = m_core.promotions->savePromotion(p);
    QVERIFY2(id != 0, qPrintable(m_core.promotions->lastError()));

    const QList<Promotion> all = m_core.promotions->promotions();
    QCOMPARE(all.size(), 1);
    const Promotion &back = all.first();
    QCOMPARE(back.id, id);
    QCOMPARE(back.name, QStringLiteral("TRES POR DOS"));
    QCOMPARE(int(back.kind), int(PromotionKind::BuyPay));
    QCOMPARE(back.buy, 3);
    QCOMPARE(back.pay, 2);
    QCOMPARE(back.from, p.from);
    QCOMPARE(back.to, p.to);
    QCOMPARE(back.weekdays, 5);
    QVERIFY(back.categoryAndBrand);
    QCOMPARE(back.rules.size(), 3);
    QCOMPARE(back.rules.at(0).name, QStringLiteral("LACTEOS"));
    QCOMPARE(back.rules.at(1).name, QStringLiteral("VACA"));
    QCOMPARE(back.rules.at(2).name, QStringLiteral("YOGUR"));
    QCOMPARE(back.rules.at(2).code, QStringLiteral("2"));
    QVERIFY(back.rules.at(2).excluded);
}

void tst_Promotions::whatItRefuses()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    fill();
    const Promotion good = [] {
        Promotion p = ofKind(PromotionKind::Percent, 10);
        p.rules = { article(QStringLiteral("1")) };
        return p;
    }();
    const auto refused = [this](const Promotion &p, const char *word) {
        QCOMPARE(m_core.promotions->savePromotion(p), qint64(0));
        QVERIFY2(m_core.promotions->lastError().contains(QString::fromUtf8(word)),
                 qPrintable(m_core.promotions->lastError()));
    };
    Promotion p = good;
    p.name = QStringLiteral("   ");
    refused(p, "nombre");
    p = good; p.value = 0;
    refused(p, "porcentaje");
    p = good; p.value = 101;
    refused(p, "porcentaje");
    p = good; p.kind = PromotionKind::FixedPrice; p.value = 0;
    refused(p, "importe");
    p = good; p.kind = PromotionKind::BuyPay; p.buy = 2; p.pay = 2;
    refused(p, "paga");
    p = good; p.kind = PromotionKind::BuyPay; p.buy = 2; p.pay = 0;
    refused(p, "paga");
    p = good; p.to = p.from.addDays(-1);
    refused(p, "fecha");
    p = good; p.from = QDate();
    refused(p, "fecha");
    p = good; p.weekdays = 0;
    refused(p, "día");
    p = good; p.rules.clear();
    refused(p, "artículos");
    p = good; p.rules = { article(QStringLiteral("1"), true) };     // only what is left out
    refused(p, "artículos");
    p = good; p.everything = true; p.inverted = true;
    refused(p, "Invertir");
    QVERIFY(m_core.promotions->promotions().isEmpty());

    p = good; p.rules = { article(QStringLiteral("no existe")) };   // and nothing half saved
    QCOMPARE(m_core.promotions->savePromotion(p), qint64(0));
    QVERIFY(m_core.promotions->promotions().isEmpty());
}

void tst_Promotions::aChangeReplacesTheRules()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    fill();
    Promotion p = ofKind(PromotionKind::Percent, 10);
    p.rules = { category(m_dairy), article(QStringLiteral("2"), true) };
    p.id = m_core.promotions->savePromotion(p);
    QVERIFY(p.id);
    p.value = 25;
    p.rules = { brand(m_cola) };
    QCOMPARE(m_core.promotions->savePromotion(p), p.id);
    const QList<Promotion> all = m_core.promotions->promotions();
    QCOMPARE(all.size(), 1);
    QCOMPARE(all.first().value, Money(25));
    QCOMPARE(all.first().rules.size(), 1);
    QCOMPARE(all.first().rules.first().target, PromotionRule::Brand);
}

void tst_Promotions::aSubcategoryIsInItsParent()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    fill();
    Promotion p = ofKind(PromotionKind::Percent, 10);
    p.rules = { category(m_dairy) };
    QVERIFY(m_core.promotions->savePromotion(p));
    QCOMPARE(m_core.promotions->offersFor(QStringLiteral("1"), QDate::currentDate())->size(), 1);   // in LECHES
    QCOMPARE(m_core.promotions->offersFor(QStringLiteral("4"), QDate::currentDate())->size(), 0);
}

void tst_Promotions::offersAreOnlyWhatRunsToday()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    fill();
    const QDate today = QDate::currentDate();
    Promotion now = ofKind(PromotionKind::Percent, 10);
    now.everything = true;
    Promotion soon = now;
    soon.from = today.addDays(1);
    Promotion past = now;
    past.from = today.addDays(-5);
    past.to = today.addDays(-1);
    Promotion otherDay = now;
    otherDay.weekdays = AguaraPromotion::AllWeekdays & ~(1 << (today.dayOfWeek() - 1));
    Promotion down = now;
    down.active = false;
    const qint64 id = m_core.promotions->savePromotion(now);
    QVERIFY(id);
    for (const Promotion &p : { soon, past, otherDay, down })
        QVERIFY(m_core.promotions->savePromotion(p));

    const auto offers = m_core.promotions->offersFor(QStringLiteral("1"), today);
    QVERIFY(offers.has_value());
    QCOMPARE(offers->size(), 1);
    QCOMPARE(offers->first().id, id);
    QCOMPARE(m_core.promotions->offersFor(QStringLiteral("1"), today.addDays(1))->size(), 3);   // tomorrow: + soon, otherDay
    QVERIFY(m_core.promotions->offersFor(QStringLiteral("no existe"), today)->isEmpty());

    QVERIFY(m_core.promotions->setPromotionActive(id, false));
    QVERIFY(m_core.promotions->offersFor(QStringLiteral("1"), today)->isEmpty());
    QCOMPARE(m_core.promotions->promotions().size(), 3);
    QCOMPARE(m_core.promotions->promotions(true).size(), 5);
}

void tst_Promotions::anArticleOutOfUseIsntListed()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    fill();
    Promotion p = ofKind(PromotionKind::Percent, 10);
    p.everything = true;
    QVERIFY(m_core.products->deactivateProduct(QStringLiteral("3")));
    QCOMPARE(codes(m_core.promotions->articlesIn(p)), QStringList({ "1", "2", "4" }));
}

void tst_Promotions::aNewCodeKeepsTheRule()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    fill();
    Promotion p = ofKind(PromotionKind::Percent, 10);
    p.rules = { article(QStringLiteral("1")) };
    QVERIFY(m_core.promotions->savePromotion(p));
    Product leche = *m_core.products->product(QStringLiteral("1"));
    leche.code = QStringLiteral("11");
    QVERIFY(m_core.products->saveProduct(leche, QStringLiteral("1")));
    QCOMPARE(m_core.promotions->promotions().first().rules.first().code, QStringLiteral("11"));
    QCOMPARE(m_core.promotions->offersFor(QStringLiteral("11"), QDate::currentDate())->size(), 1);
}

// --- in a sale ---------------------------------------------------------------

void tst_Promotions::theSaleKeepsWhatItWasGiven()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    fill();
    QVERIFY(openTill());
    Promotion p = ofKind(PromotionKind::Percent, 20);
    p.name = QStringLiteral("SEMANA");
    p.everything = true;
    p.id = m_core.promotions->savePromotion(p);

    SaleDraft draft;
    SaleLine line = lineOf(10000, AguaraQuantity::whole(2));
    line.promotionId = p.id;
    line.promotionName = p.name;
    line.promotionDiscount = 4000;
    line.discount = 1000;
    draft.lines << line;
    draft.totalDiscount = 1500;
    const SaleTotals totals = AguaraSale::totals(draft);
    QCOMPARE(totals.net, Money(13500));
    QCOMPARE(totals.discount, Money(6500));

    const qint64 id = sell(draft);
    QVERIFY2(id != 0, qPrintable(m_core.sales->lastError()));

    p.name = QStringLiteral("OTRO NOMBRE");     // later changes don't reach it
    QVERIFY(m_core.promotions->savePromotion(p));
    const SaleItem item = m_core.sales->sale(id)->totals.items.first();
    QCOMPARE(item.promotionId, p.id);
    QCOMPARE(item.promotionName, QStringLiteral("SEMANA"));
    QCOMPARE(item.promotionDiscount, Money(4000));
    QCOMPARE(item.discount, Money(6500));
    QCOMPARE(item.net, Money(13500));
    QCOMPARE(item.vat, AguaraMoney::vatIncluded(13500, 10));
}

void tst_Promotions::aPromotionNeedsNoRoomInTheManualDiscount()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    fill();
    SaleDraft draft;
    SaleLine line = lineOf(10000);
    line.code = QStringLiteral("1");
    line.promotionId = 1;
    line.promotionDiscount = 9000;
    draft.lines << line;
    draft.totalDiscount = 5000;                 // on what is left, not on the price
    QCOMPARE(AguaraSale::totals(draft).net, Money(0));
}

void tst_Promotions::itSurvivesTheQueue()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    fill();
    SaleDraft draft;
    SaleLine line = lineOf(10000);
    line.promotionId = 7;
    line.promotionName = QStringLiteral("SEMANA");
    line.promotionDiscount = 2000;
    draft.lines << line;
    const SaleDraft back = SaleJson::toDraft(SaleJson::fromDraft(draft));
    QCOMPARE(back.lines.first().promotionId, qint64(7));
    QCOMPARE(back.lines.first().promotionName, QStringLiteral("SEMANA"));
    QCOMPARE(back.lines.first().promotionDiscount, Money(2000));

    // a sale queued by the version before promotions
    const SaleDraft older = SaleJson::toDraft(QByteArrayLiteral(
        "{\"lines\":[{\"code\":\"1\",\"quantity\":\"1000\",\"unitPrice\":\"500\",\"discount\":\"0\"}]}"));
    QCOMPARE(older.lines.first().promotionId, qint64(0));
    QCOMPARE(older.lines.first().promotionDiscount, Money(0));
}

void tst_Promotions::aDiscountThatPassesTheLineIsRefused()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    fill();
    QVERIFY(openTill());
    SaleDraft draft;
    SaleLine line = lineOf(10000);
    line.promotionDiscount = 9000;
    line.discount = 2000;
    draft.lines << line;
    QCOMPARE(m_core.sales->checkout(draft, { inCash(0) }), qint64(0));
    QVERIFY(m_core.sales->lastError().contains(QStringLiteral("descuento")));
}

void tst_Promotions::aReturnPaysWhatWasCharged()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    fill();
    QVERIFY(openTill());
    Promotion p = ofKind(PromotionKind::Percent, 50);
    p.everything = true;
    p.id = m_core.promotions->savePromotion(p);
    SaleDraft draft;
    SaleLine line = lineOf(10000, AguaraQuantity::whole(2));
    line.promotionId = p.id;
    line.promotionName = p.name;
    line.promotionDiscount = 10000;
    draft.lines << line;
    const qint64 sale = sell(draft);
    QVERIFY(sale);

    ReturnRequest r;
    r.saleId = sale;
    ReturnLine one;
    one.line = 1;
    one.quantity = AguaraQuantity::One;
    r.lines << one;
    r.method = QString::fromLatin1(PaymentMethod::Cash);
    r.reason = QStringLiteral("no le gustó");
    r.authorizedBy = QStringLiteral("encargado");
    const qint64 back = m_core.returns->processReturn(r);
    QVERIFY2(back != 0, qPrintable(m_core.returns->lastError()));
    QCOMPARE(m_core.returns->returnRecord(back)->items.first().amount, Money(5000));
}

void tst_Promotions::theReportIsNetOfReturns()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    fill();
    QVERIFY(openTill());
    Promotion p = ofKind(PromotionKind::Percent, 50);
    p.name = QStringLiteral("MITAD");
    p.everything = true;
    p.id = m_core.promotions->savePromotion(p);
    const auto saleOf = [&](Quantity units, bool promoted) {
        SaleDraft draft;
        SaleLine line = lineOf(10000, AguaraQuantity::whole(units));
        if (promoted) {
            line.promotionId = p.id;
            line.promotionName = p.name;
            line.promotionDiscount = AguaraPromotion::discount(p, line);
        }
        draft.lines << line;
        return sell(draft);
    };
    const qint64 first = saleOf(2, true);       // charged 10.000, 10.000 given
    QVERIFY(first);
    QVERIFY(saleOf(1, true));                   // charged 5.000, 5.000 given
    QVERIFY(saleOf(3, false));                  // not in it

    ReturnRequest r;
    r.saleId = first;
    ReturnLine one;
    one.line = 1;
    one.quantity = AguaraQuantity::One;
    r.lines << one;
    r.method = QString::fromLatin1(PaymentMethod::Cash);
    r.reason = QStringLiteral("cambio");
    r.authorizedBy = QStringLiteral("encargado");
    QVERIFY(m_core.returns->processReturn(r));

    const std::optional<ReportResult> report = m_core.reports->run(QStringLiteral("ventas.promocion"), ReportFilter());
    QVERIFY2(report.has_value(), qPrintable(m_core.reports->lastError()));
    QCOMPARE(report->rows.size(), 1);
    const QVector<QVariant> row = report->rows.first();
    QCOMPARE(row.at(0).toString(), QStringLiteral("MITAD"));
    QCOMPARE(row.at(1).toInt(), 2);             // tickets
    QCOMPARE(row.at(2).toInt(), 2);             // lines
    QCOMPARE(row.at(3).toLongLong(), Money(10000));     // 15.000 given, 5.000 back
    QCOMPARE(row.at(4).toLongLong(), Money(10000));     // 15.000 charged, 5.000 back
}

QTEST_GUILESS_MAIN(tst_Promotions)
#include "tst_promotions.moc"
