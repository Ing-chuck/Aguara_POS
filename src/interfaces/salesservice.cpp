#include "salesservice.h"

#include <QCoreApplication>

namespace AguaraSale {

Money lineGross(const SaleLine &line)
{
    return line.fixedAmount > 0 ? line.fixedAmount : AguaraQuantity::lineTotal(line.unitPrice, line.quantity);
}

SaleTotals totals(const SaleDraft &draft)
{
    SaleTotals t;
    Money afterLineDiscounts = 0;
    for (const SaleLine &line : draft.lines) {
        SaleItem item;
        item.code = line.code;
        item.description = line.description;
        item.unit = line.unit;
        item.decimals = line.decimals;
        item.quantity = line.quantity;
        item.unitPrice = line.unitPrice;
        item.vatRate = line.vatRate;
        item.gross = lineGross(line);
        item.discount = line.discount;
        item.net = item.gross - item.discount;
        afterLineDiscounts += item.net;
        t.items << item;
    }

    // Each line carries its share of the sale's discount, so that IVA is on
    // what was really charged. The running share keeps the sum exact.
    const Money saleDiscount = qBound(Money(0), draft.totalDiscount, afterLineDiscounts);
    Money runningBase = 0;
    Money shared = 0;
    for (SaleItem &item : t.items) {
        if (afterLineDiscounts <= 0)
            break;
        const Money base = item.net;
        runningBase += base;
        const Money reached = (saleDiscount * runningBase + afterLineDiscounts / 2) / afterLineDiscounts;
        const Money share = reached - shared;
        shared = reached;
        item.discount += share;
        item.net -= share;
    }

    for (SaleItem &item : t.items) {
        item.vat = AguaraMoney::vatIncluded(item.net, item.vatRate);
        t.gross += item.gross;
        t.discount += item.discount;
        t.net += item.net;
        if (item.vatRate == 10) {
            t.taxed10 += item.net;
            t.vat10 += item.vat;
        } else if (item.vatRate == 5) {
            t.taxed5 += item.net;
            t.vat5 += item.vat;
        } else {
            t.exempt += item.net;
        }
    }

    t.total = t.net;
    return t;
}

bool isElectronic(const QString &method)
{
    return method == QLatin1String(PaymentMethod::Debit) || method == QLatin1String(PaymentMethod::Credit)
           || method == QLatin1String(PaymentMethod::Transfer);
}

QString methodName(const QString &method)
{
    if (method == QLatin1String(PaymentMethod::Cash))
        return QCoreApplication::translate("AguaraSale", "Efectivo");
    if (method == QLatin1String(PaymentMethod::Debit))
        return QCoreApplication::translate("AguaraSale", "Tarjeta de débito");
    if (method == QLatin1String(PaymentMethod::Credit))
        return QCoreApplication::translate("AguaraSale", "Tarjeta de crédito");
    if (method == QLatin1String(PaymentMethod::Transfer))
        return QCoreApplication::translate("AguaraSale", "Transferencia");
    return method;
}

QString normalizeReference(const QString &reference)
{
    return reference.trimmed().toUpper();
}

QString formatNumber(qint64 registerId, qint64 number)
{
    return QStringLiteral("%1-%2").arg(registerId, 3, 10, QLatin1Char('0')).arg(number, 6, 10, QLatin1Char('0'));
}

} // namespace AguaraSale
