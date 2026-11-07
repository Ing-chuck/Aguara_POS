#ifndef PROMOTIONSERVICE_H
#define PROMOTIONSERVICE_H

#include "apiversion.h"
#include "aguara_global.h"
#include "money.h"
#include "productservice.h"
#include "quantity.h"
#include "salesservice.h"

#include <QDate>
#include <QList>
#include <QString>
#include <QtPlugin>

#include <optional>

enum class PromotionKind {
    Percent,        ///< `value` percent off
    AmountOff,      ///< `value` guaraníes off each unit
    FixedPrice,     ///< each unit at `value` guaraníes
    BuyPay          ///< take `buy`, pay `pay`
};

/// One entry of what a promotion picks out or leaves out.
struct PromotionRule {
    enum Target { Article, Category, Brand };
    Target  target = Article;
    QString code;               ///< of an Article
    qint64  id = 0;             ///< of a Category, with its subcategories, or a Brand
    QString name;               ///< to show it. Read only.
    bool    excluded = false;   ///< leaves it out, whatever else picks it
};

/// A discount with dates. Which articles it reaches:
///
///     `everything`, or what the rules pick         the selection
///     `inverted`: everything but that              turned around
///     excluded rules: out, always                  the last word
///
/// Rules of a category and of a brand add up, unless `categoryAndBrand` asks
/// for both at once ("Bebidas" of one brand). An Article rule always adds.
struct Promotion {
    qint64  id = 0;
    QString name;
    PromotionKind kind = PromotionKind::Percent;
    Money   value = 0;          ///< see PromotionKind
    int     buy = 0;
    int     pay = 0;
    Quantity minimum = 0;       ///< on the line, to get it. 0: from the first unit.
    QDate   from;
    QDate   to;                 ///< inclusive
    int     weekdays = 127;     ///< bit 0 monday ... bit 6 sunday
    bool    everything = false;
    bool    categoryAndBrand = false;
    bool    inverted = false;
    bool    active = true;
    QList<PromotionRule> rules;
};

namespace AguaraPromotion {

constexpr int AllWeekdays = 127;

/// Is it running that day? Active, within its dates, on one of its weekdays.
AGUARA_EXPORT bool runsOn(const Promotion &promotion, const QDate &day);

/// Does it reach this article? `categories` holds the article's category and
/// the ones above it.
AGUARA_EXPORT bool covers(const Promotion &promotion, const QString &code, qint64 brandId,
                          const QList<qint64> &categories);

/// What it takes off this line, never more than the line. 0: below its minimum.
AGUARA_EXPORT Money discount(const Promotion &promotion, const SaleLine &line);

/// Of `offers`, the one that gives this line most: the customer's best deal.
/// The oldest of equals. -1: none gives anything.
AGUARA_EXPORT int best(const QList<Promotion> &offers, const SaleLine &line);

/// "20 %", "Gs. 2.000 menos", "A Gs. 9.000", "Lleva 3, paga 2".
AGUARA_EXPORT QString describe(const Promotion &promotion);
/// "Todos los días", "Lun, Mié".
AGUARA_EXPORT QString weekdayNames(int weekdays);

} // namespace AguaraPromotion

/// The promotions. Provided by the core; the till applies them as it rings up.
/// Every method returns false, 0 or nothing on failure, with the reason in
/// lastError().
class PromotionService {
public:
    virtual ~PromotionService() = default;

    /// Latest to end first. Without `inactive`, those not taken down.
    virtual QList<Promotion> promotions(bool inactive = false) const = 0;
    /// New (id 0) or changed. Past sales keep the name and the discount they had.
    /// Check promociones.editar first. Returns the id.
    virtual qint64 savePromotion(const Promotion &promotion) = 0;
    /// Takes it down, or brings it back. Past sales stay as they are.
    virtual bool setPromotionActive(qint64 id, bool active) = 0;

    /// The promotions running on `day` that reach this article. Without a value
    /// when the database can't say, as in an outage: what was known still holds.
    virtual std::optional<QList<Promotion>> offersFor(const QString &code, const QDate &day) const = 0;
    /// The active articles it reaches, by description: for a promotion not saved
    /// too. The dates don't matter here.
    virtual QList<Product> articlesIn(const Promotion &promotion) const = 0;

    /// Why the last call failed, in Spanish for the user.
    virtual QString lastError() const = 0;
};

#define PromotionService_iid "org.AguaraPOS.PromotionService/" AGUARA_API_VERSION
Q_DECLARE_INTERFACE(PromotionService, PromotionService_iid)

#endif // PROMOTIONSERVICE_H
