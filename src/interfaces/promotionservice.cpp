#include "promotionservice.h"

#include <QCoreApplication>
#include <QStringList>

namespace AguaraPromotion {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("AguaraPromotion", text);
}

} // namespace

bool runsOn(const Promotion &p, const QDate &day)
{
    return p.active && day >= p.from && day <= p.to && (p.weekdays & (1 << (day.dayOfWeek() - 1)));
}

bool covers(const Promotion &p, const QString &code, qint64 brandId, const QList<qint64> &categories)
{
    bool listed = false, inCategory = false, inBrand = false;
    bool anyCategory = false, anyBrand = false;
    for (const PromotionRule &rule : p.rules) {
        const bool matches = rule.target == PromotionRule::Article ? rule.code == code
                           : rule.target == PromotionRule::Category ? categories.contains(rule.id)
                           : rule.id == brandId;
        if (rule.excluded) {
            if (matches)
                return false;           // the last word
            continue;
        }
        switch (rule.target) {
        case PromotionRule::Article:  listed = listed || matches; break;
        case PromotionRule::Category: anyCategory = true; inCategory = inCategory || matches; break;
        case PromotionRule::Brand:    anyBrand = true; inBrand = inBrand || matches; break;
        }
    }
    const bool both = p.categoryAndBrand && anyCategory && anyBrand;
    const bool grouped = both ? inCategory && inBrand : inCategory || inBrand;
    const bool chosen = p.everything || listed || grouped;
    return p.inverted ? !chosen : chosen;
}

Money discount(const Promotion &p, const SaleLine &line)
{
    if (line.quantity <= 0 || line.quantity < p.minimum)
        return 0;
    const Money gross = AguaraSale::lineGross(line);
    Money off = 0;
    switch (p.kind) {
    case PromotionKind::Percent:
        off = (gross * p.value + 50) / 100;
        break;
    case PromotionKind::AmountOff:
        off = AguaraQuantity::lineTotal(p.value, line.quantity);
        break;
    case PromotionKind::FixedPrice:
        off = gross - AguaraQuantity::lineTotal(p.value, line.quantity);    // a scale's label too
        break;
    case PromotionKind::BuyPay:
        if (p.buy > 0) {
            const qint64 sets = line.quantity / AguaraQuantity::whole(p.buy);
            off = AguaraQuantity::lineTotal(line.unitPrice, AguaraQuantity::whole(sets * (p.buy - p.pay)));
        }
        break;
    }
    return qBound(Money(0), off, gross);
}

int best(const QList<Promotion> &offers, const SaleLine &line)
{
    int found = -1;
    Money most = 0;
    for (int i = 0; i < offers.size(); ++i) {
        const Money off = discount(offers.at(i), line);
        if (off > most || (off == most && off > 0 && offers.at(i).id < offers.at(found).id)) {
            most = off;
            found = i;
        }
    }
    return found;
}

QString describe(const Promotion &p)
{
    switch (p.kind) {
    case PromotionKind::Percent:    return QStringLiteral("%1 %").arg(p.value);
    case PromotionKind::AmountOff:  return tr("%1 menos").arg(AguaraMoney::formatWithSymbol(p.value));
    case PromotionKind::FixedPrice: return tr("A %1").arg(AguaraMoney::formatWithSymbol(p.value));
    case PromotionKind::BuyPay:     return tr("Lleva %1, paga %2").arg(p.buy).arg(p.pay);
    }
    return QString();
}

QString weekdayNames(int weekdays)
{
    if ((weekdays & AllWeekdays) == AllWeekdays)
        return tr("Todos los días");
    static const char *const names[] = { "Lun", "Mar", "Mié", "Jue", "Vie", "Sáb", "Dom" };
    QStringList chosen;
    for (int day = 0; day < 7; ++day) {
        if (weekdays & (1 << day))
            chosen << tr(names[day]);
    }
    return chosen.join(QStringLiteral(", "));
}

} // namespace AguaraPromotion
