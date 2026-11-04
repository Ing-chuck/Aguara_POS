#include "closingsheet.h"

#include <salesservice.h>

#include <QCoreApplication>
#include <QPrinter>
#include <QTextDocument>

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("ClosingSheet", text);
}

QString row(const QString &label, const QString &value, bool strong = false)
{
    const QString open = strong ? QStringLiteral("<b>") : QString();
    const QString close = strong ? QStringLiteral("</b>") : QString();
    return QStringLiteral("<tr><td>%1%2%3</td><td align=\"right\">%1%4%3</td></tr>")
        .arg(open, label.toHtmlEscaped(), close, value.toHtmlEscaped());
}

QString money(Money amount)
{
    return AguaraMoney::formatWithSymbol(amount);
}

QString section(const QString &title, const QString &rows)
{
    return QStringLiteral("<h3>%1</h3><table width=\"100%\" cellspacing=\"0\" cellpadding=\"3\">%2</table>")
        .arg(title.toHtmlEscaped(), rows);
}

QString countRows(const CashCount &count)
{
    QString rows;
    for (const DenominationCount &d : count) {
        rows += row(QStringLiteral("%1 x %2").arg(d.count).arg(AguaraMoney::format(d.denomination)),
                    money(d.denomination * d.count));
    }
    return rows;
}

QString movementName(const QString &type)
{
    if (type == QLatin1String(CashMovementType::In))
        return tr("Ingreso");
    return type == QLatin1String(CashMovementType::Out) ? tr("Egreso") : tr("Retiro");
}

} // namespace

QString ClosingSheet::html(const CashSessionReport &r)
{
    const CashSession &s = r.session;
    const QString dateTime = QStringLiteral("dd/MM/yyyy HH:mm");

    QString body = QStringLiteral("<h2>%1 - %2</h2>").arg(tr("Planilla de cierre"), s.registerName.toHtmlEscaped());
    body += section(tr("Sesión"),
        row(tr("Apertura"), s.opened.toString(dateTime) + QStringLiteral(" - ") + s.openedBy)
        + row(tr("Cierre"), s.closed.toString(dateTime) + QStringLiteral(" - ") + s.closedBy));

    body += section(tr("Ventas"),
        row(tr("Ventas cobradas"), QString::number(r.salesCount))
        + row(tr("Descuentos"), money(r.discounts))
        + row(tr("Redondeo"), money(r.rounding))
        + row(tr("Anuladas (%1)").replace(QLatin1String("%1"), QString::number(r.voidedCount)), money(r.voidedTotal))
        + row(tr("Total vendido"), money(r.salesTotal), true));

    body += section(tr("IVA"),
        row(tr("Gravado 10 %"), money(r.taxed10)) + row(tr("IVA 10 %"), money(r.vat10))
        + row(tr("Gravado 5 %"), money(r.taxed5)) + row(tr("IVA 5 %"), money(r.vat5))
        + row(tr("Exento"), money(r.exempt)));

    QString payments;
    for (const PaymentTotal &p : r.payments)
        payments += row(AguaraSale::methodName(p.method), money(p.amount));
    body += section(tr("Por medio de pago"), payments.isEmpty() ? row(tr("Sin ventas"), QString()) : payments);

    if (!r.vouchers.isEmpty()) {
        QString vouchers;
        for (const PaymentVoucher &v : r.vouchers) {
            vouchers += row(QStringLiteral("%1 - %2 - %3").arg(AguaraSale::methodName(v.method), v.reference, v.saleNumber),
                            money(v.amount));
        }
        body += section(tr("Comprobantes por conciliar"), vouchers);
    }

    if (!r.returns.isEmpty()) {
        QString returns;
        for (const SessionReturn &d : r.returns) {
            returns += row(QStringLiteral("%1 de la venta %2 - %3 %4 - %5 (autorizó %6)")
                               .arg(d.number, d.saleNumber, AguaraSale::methodName(d.method), d.reference, d.reason, d.authorizedBy),
                           money(-d.amount));
        }
        body += section(tr("Devoluciones"), returns);
    }

    if (!r.movements.isEmpty()) {
        QString movements;
        for (const CashMovement &m : r.movements) {
            movements += row(QStringLiteral("%1 %2 - %3 (%4)").arg(m.when.toString(QStringLiteral("HH:mm")),
                                                                   movementName(m.type), m.reason, m.user),
                             money(m.type == QLatin1String(CashMovementType::In) ? m.amount : -m.amount));
        }
        body += section(tr("Movimientos de caja"), movements);
    }

    body += section(tr("Arqueo"),
        row(tr("Fondo inicial"), money(s.openingFloat))
        + row(tr("Ventas en efectivo y otros ingresos"), money(r.expectedCash - s.openingFloat + r.cashOut + r.cashRefunds))
        + row(tr("Devoluciones en efectivo"), money(-r.cashRefunds))
        + row(tr("Egresos y retiros"), money(-r.cashOut))
        + row(tr("Esperado"), money(r.expectedCash), true)
        + row(tr("Contado"), money(s.counted), true)
        + row(s.difference < 0 ? tr("Faltante") : tr("Diferencia"), money(s.difference), true));
    if (s.difference != 0) {
        body += section(tr("Diferencia autorizada"),
            row(tr("Motivo"), s.differenceReason) + row(tr("Autorizó"), s.authorizedBy));
    }

    body += section(tr("Conteo de cierre"), countRows(r.closingCount));
    return QStringLiteral("<html><body style=\"font-size: 10pt;\">%1</body></html>").arg(body);
}

Receipt ClosingSheet::receipt(const CashSessionReport &r)
{
    const CashSession &s = r.session;
    const QString dateTime = QStringLiteral("dd/MM/yyyy HH:mm");
    Receipt out;
    out.center(tr("Planilla de cierre"), ReceiptLine::Title);
    out.center(s.registerName, ReceiptLine::Strong);
    out.rule();
    out.add(tr("Apertura"), s.opened.toString(dateTime));
    out.add(QString(), s.openedBy);
    out.add(tr("Cierre"), s.closed.toString(dateTime));
    out.add(QString(), s.closedBy);
    out.rule();

    out.add(tr("Ventas cobradas"), QString::number(r.salesCount));
    out.add(tr("Descuentos"), money(r.discounts));
    out.add(tr("Redondeo"), money(r.rounding));
    out.add(tr("Anuladas (%1)").replace(QLatin1String("%1"), QString::number(r.voidedCount)), money(r.voidedTotal));
    out.add(tr("Total vendido"), money(r.salesTotal), ReceiptLine::Strong);
    out.rule();

    out.add(tr("Gravado 10 %"), money(r.taxed10));
    out.add(tr("IVA 10 %"), money(r.vat10));
    out.add(tr("Gravado 5 %"), money(r.taxed5));
    out.add(tr("IVA 5 %"), money(r.vat5));
    out.add(tr("Exento"), money(r.exempt));
    out.rule();

    out.add(tr("Por medio de pago"), QString(), ReceiptLine::Strong);
    for (const PaymentTotal &p : r.payments)
        out.add(AguaraSale::methodName(p.method), money(p.amount));
    if (!r.vouchers.isEmpty()) {
        out.rule();
        out.add(tr("Comprobantes por conciliar"), QString(), ReceiptLine::Strong);
        for (const PaymentVoucher &v : r.vouchers)
            out.add(QStringLiteral("%1 %2 %3").arg(AguaraSale::methodName(v.method), v.reference, v.saleNumber), money(v.amount));
    }
    if (!r.returns.isEmpty()) {
        out.rule();
        out.add(tr("Devoluciones"), QString(), ReceiptLine::Strong);
        for (const SessionReturn &d : r.returns) {
            out.add(QStringLiteral("%1 %2 %3").arg(d.number, AguaraSale::methodName(d.method), d.reference), money(-d.amount));
            out.add(QStringLiteral("  %1 (%2)").arg(d.reason, d.authorizedBy));
        }
    }
    if (!r.movements.isEmpty()) {
        out.rule();
        out.add(tr("Movimientos de caja"), QString(), ReceiptLine::Strong);
        for (const CashMovement &m : r.movements) {
            out.add(QStringLiteral("%1 %2 - %3 (%4)").arg(m.when.toString(QStringLiteral("HH:mm")), movementName(m.type), m.reason, m.user),
                    money(m.type == QLatin1String(CashMovementType::In) ? m.amount : -m.amount));
        }
    }
    out.rule();

    out.add(tr("Fondo inicial"), money(s.openingFloat));
    out.add(tr("Ventas en efectivo y otros ingresos"), money(r.expectedCash - s.openingFloat + r.cashOut + r.cashRefunds));
    out.add(tr("Devoluciones en efectivo"), money(-r.cashRefunds));
    out.add(tr("Egresos y retiros"), money(-r.cashOut));
    out.add(tr("Esperado"), money(r.expectedCash), ReceiptLine::Strong);
    out.add(tr("Contado"), money(s.counted), ReceiptLine::Strong);
    out.add(s.difference < 0 ? tr("Faltante") : tr("Diferencia"), money(s.difference), ReceiptLine::Strong);
    if (s.difference != 0) {
        out.add(tr("Motivo: %1").replace(QLatin1String("%1"), s.differenceReason));
        out.add(tr("Autorizó: %1").replace(QLatin1String("%1"), s.authorizedBy));
    }
    return out;
}

void ClosingSheet::print(QPrinter *printer, const CashSessionReport &report)
{
    QTextDocument document;
    document.setHtml(html(report));
    document.print(printer);
}
