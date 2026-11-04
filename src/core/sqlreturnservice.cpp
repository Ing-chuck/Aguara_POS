#include "sqlreturnservice.h"
#include "connectionmonitor.h"
#include "errors.h"
#include "sqlcashservice.h"
#include "sqlsalesservice.h"
#include "stockledger.h"

#include <inventoryservice.h>
#include <userservice.h>

#include <QHash>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

SqlReturnService::SqlReturnService(ConnectionMonitor *monitor, SqlSalesService *sales, SqlCashService *cash, QObject *parent)
    : QObject(parent), m_monitor(monitor), m_sales(sales), m_cash(cash), m_connectionName(monitor->connectionName())
{
}

QSqlDatabase SqlReturnService::db() const
{
    return QSqlDatabase::database(m_connectionName, false);
}

bool SqlReturnService::exec(QSqlQuery &q) const
{
    if (m_monitor->exec(q, m_inTransaction))
        return true;
    m_lastError = m_monitor->lastError();
    return false;
}

std::optional<ReturnableSale> SqlReturnService::findSale(const QString &number) const
{
    static const QRegularExpression shape(QStringLiteral("^(?:([0-9]+)-)?([0-9]+)$"));
    const QRegularExpressionMatch match = shape.match(number.trimmed());
    if (!match.hasMatch()) {
        m_lastError = tr("Escriba el número de la venta: 001-000123.");
        return std::nullopt;
    }
    const qint64 registerId = match.captured(1).isEmpty() ? m_cash->currentRegister().id : match.captured(1).toLongLong();
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT id FROM ventas WHERE caja_id = ? AND numero = ?"));
    q.addBindValue(registerId);
    q.addBindValue(match.captured(2).toLongLong());
    if (!exec(q))
        return std::nullopt;
    if (!q.next()) {
        m_lastError = tr("No hay ninguna venta con ese número.");
        return std::nullopt;
    }
    return returnable(q.value(0).toLongLong());
}

std::optional<ReturnableSale> SqlReturnService::returnable(qint64 saleId) const
{
    const std::optional<SaleRecord> sale = m_sales->sale(saleId);
    if (!sale) {
        m_lastError = m_sales->lastError();
        return std::nullopt;
    }
    ReturnableSale result;
    result.sale = *sale;
    int line = 0;
    for (const SaleItem &item : sale->totals.items) {
        ReturnableItem r;
        r.line = ++line;
        r.code = item.code;
        r.description = item.description;
        r.unit = item.unit;
        r.decimals = item.decimals;
        r.sold = item.quantity;
        r.net = item.net;
        r.vatRate = item.vatRate;
        result.items << r;
    }

    QSqlQuery done(db());
    done.prepare(QStringLiteral(
        "SELECT i.linea, CAST(SUM(i.cantidad) AS BIGINT), CAST(SUM(i.importe) AS BIGINT) FROM devoluciones_items i"
        " JOIN devoluciones d ON d.id = i.devolucion_id WHERE d.venta_id = ? GROUP BY i.linea"));
    done.addBindValue(saleId);
    if (!exec(done))
        return std::nullopt;
    while (done.next()) {
        for (ReturnableItem &item : result.items) {
            if (item.line == done.value(0).toInt()) {
                item.returned = done.value(1).toLongLong();
                item.refunded = done.value(2).toLongLong();
            }
        }
    }

    QHash<QString, Money> gone;
    QSqlQuery refunds(db());
    refunds.prepare(QStringLiteral("SELECT medio, CAST(SUM(pagado) AS BIGINT) FROM devoluciones WHERE venta_id = ? GROUP BY medio"));
    refunds.addBindValue(saleId);
    if (!exec(refunds))
        return std::nullopt;
    while (refunds.next())
        gone.insert(refunds.value(0).toString(), refunds.value(1).toLongLong());
    for (const SalePayment &p : sale->payments) {
        PaymentTotal left;
        left.method = p.method;
        left.amount = p.amount - p.change - gone.value(p.method);
        gone.remove(p.method);                  // two payments alike: count it once
        if (left.amount > 0)
            result.refundable << left;
    }
    return result;
}

bool SqlReturnService::checkRequest(const ReturnRequest &r, const ReturnableSale &sale, Money *amount) const
{
    if (sale.sale.state != QLatin1String(SaleState::Paid)) {
        m_lastError = tr("La venta está anulada.");
        return false;
    }
    if (r.reason.trimmed().isEmpty()) {
        m_lastError = tr("Indique el motivo de la devolución.");
        return false;
    }
    bool any = false;
    for (const ReturnLine &line : r.lines) {
        if (line.quantity == 0)
            continue;
        const ReturnableItem *found = nullptr;
        for (const ReturnableItem &item : sale.items) {
            if (item.line == line.line)
                found = &item;
        }
        if (!found || line.quantity < 0) {
            m_lastError = tr("Revise las cantidades a devolver.");
            return false;
        }
        if (line.quantity > found->sold - found->returned) {
            m_lastError = tr("De %1 se pueden devolver %2.")
                              .arg(found->description, AguaraQuantity::format(found->sold - found->returned, found->decimals));
            return false;
        }
        any = true;
    }
    if (!any) {
        m_lastError = tr("Indique qué se devuelve.");
        return false;
    }

    *amount = AguaraReturn::amount(sale, r.lines);
    if (r.method == QLatin1String(PaymentMethod::CreditNote))
        return true;                            // store credit fits any sale

    Money left = -1;
    for (const PaymentTotal &p : sale.refundable) {
        if (p.method == r.method)
            left = p.amount;
    }
    if (left < 0) {
        m_lastError = tr("La venta no se pagó con %1: elija uno de sus medios o una nota de crédito.")
                          .arg(AguaraSale::methodName(r.method));
        return false;
    }
    // In parts, cash can round up a few guaraníes past what's left: then it
    // gives what's left (writeReturn())
    const Money slack = r.method == QLatin1String(PaymentMethod::Cash) ? AguaraSale::CashStep - 1 : 0;
    if (AguaraReturn::paid(r.method, *amount) - slack > left) {
        m_lastError = tr("Con %1 quedan Gs. %2 por devolver. Haga la devolución en partes o con una nota de crédito.")
                          .arg(AguaraSale::methodName(r.method), AguaraMoney::format(left));
        return false;
    }
    return true;
}

qint64 SqlReturnService::processReturn(const ReturnRequest &r)
{
    if (!m_monitor->isConnected()) {
        m_lastError = tr("Sin conexión con el servidor: las devoluciones esperan a que vuelva.");
        return 0;
    }
    const std::optional<CashSession> session = m_cash->currentSession();
    if (!session || session->isStale()) {
        m_lastError = tr("Abra la caja para hacer una devolución: su dinero sale de la sesión.");
        return 0;
    }
    const std::optional<ReturnableSale> sale = returnable(r.saleId);
    if (!sale)
        return 0;
    Money amount = 0;
    if (!checkRequest(r, *sale, &amount))
        return 0;

    if (r.method != QLatin1String(PaymentMethod::Cash) && r.method != QLatin1String(PaymentMethod::CreditNote)) {
        for (const PaymentMethodInfo &m : m_sales->paymentMethods()) {
            if (m.code == r.method && m.needsCode && AguaraSale::normalizeReference(r.reference).isEmpty()) {
                m_lastError = tr("Revierta el pago y escriba el código del comprobante.");
                return 0;
            }
        }
    }
    if (r.method == QLatin1String(PaymentMethod::Cash)) {
        const std::optional<CashSessionReport> report = m_cash->sessionReport(session->id);
        if (report && AguaraReturn::paid(r.method, amount) > report->expectedCash) {
            m_lastError = tr("No hay efectivo suficiente en la caja: use una nota de crédito.");
            return 0;
        }
    }

    if (m_monitor->connectionBroken())
        m_monitor->reconnect();
    if (!db().transaction()) {
        m_lastError = AguaraErrors::logAndExplain(db().lastError(), QStringLiteral("BEGIN"));
        return 0;
    }
    m_inTransaction = true;
    const qint64 id = writeReturn(r, *sale, *session, amount);
    if (id == 0) {
        m_inTransaction = false;
        db().rollback();
        return 0;
    }
    m_inTransaction = false;
    QSqlDatabase database = db();
    if (!database.commit()) {
        m_lastError = AguaraErrors::logAndExplain(database.lastError(), QStringLiteral("COMMIT"));
        database.rollback();
        return 0;
    }
    if (m_users)
        m_users->audit(QStringLiteral("venta.devolver"), QStringLiteral("venta"),
                       AguaraSale::formatNumber(sale->sale.registerId, sale->sale.number), QString(),
                       tr("Gs. %1 por %2, autorizó %3: %4").arg(AguaraMoney::format(amount), AguaraSale::methodName(r.method),
                                                                 r.authorizedBy, r.reason.trimmed()));
    return id;
}

qint64 SqlReturnService::writeReturn(const ReturnRequest &r, const ReturnableSale &sale, const CashSession &session, Money amount)
{
    const QString user = m_users && m_users->currentUser() ? m_users->currentUser()->username : QString();
    const QString number = AguaraSale::formatNumber(sale.sale.registerId, sale.sale.number);
    Money paid = AguaraReturn::paid(r.method, amount);
    for (const PaymentTotal &p : sale.refundable) {
        if (p.method == r.method)
            paid = qMin(paid, p.amount);        // never more than what came in
    }

    QSqlQuery head(db());
    head.prepare(QStringLiteral(
        "INSERT INTO devoluciones (venta_id, caja_id, sesion_id, usuario, autorizo, motivo, importe, pagado, medio, referencia)"
        " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    head.addBindValue(sale.sale.id);
    head.addBindValue(session.registerId);
    head.addBindValue(session.id);
    head.addBindValue(user);
    head.addBindValue(r.authorizedBy);
    head.addBindValue(r.reason.trimmed());
    head.addBindValue(amount);
    head.addBindValue(paid);
    head.addBindValue(r.method);
    head.addBindValue(AguaraSale::normalizeReference(r.reference));
    if (!exec(head))
        return 0;
    const qint64 id = head.lastInsertId().toLongLong();

    QHash<int, Money> costs;
    QSqlQuery cost(db());
    cost.prepare(QStringLiteral("SELECT linea, costo_unitario FROM ventas_items WHERE venta_id = ?"));
    cost.addBindValue(sale.sale.id);
    if (!exec(cost))
        return 0;
    while (cost.next())
        costs.insert(cost.value(0).toInt(), cost.value(1).toLongLong());
    cost.finish();

    StockLedger ledger(m_monitor, m_connectionName, &m_lastError, m_users);
    for (const ReturnLine &line : r.lines) {
        if (line.quantity == 0)
            continue;
        for (const ReturnableItem &item : sale.items) {
            if (item.line != line.line)
                continue;
            const Money part = AguaraReturn::amount(sale, { line });
            QSqlQuery row(db());
            row.prepare(QStringLiteral(
                "INSERT INTO devoluciones_items (devolucion_id, linea, producto_codigo, descripcion, unidad, decimales,"
                " cantidad, importe, iva) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"));
            row.addBindValue(id);
            row.addBindValue(item.line);
            row.addBindValue(item.code);
            row.addBindValue(item.description);
            row.addBindValue(item.unit);
            row.addBindValue(item.decimals);
            row.addBindValue(line.quantity);
            row.addBindValue(part);
            row.addBindValue(AguaraMoney::vatIncluded(part, item.vatRate));
            if (!exec(row) || !ledger.move(item.code, StockMovementType::Return, line.quantity, costs.value(item.line),
                                           r.reason.trimmed(), tr("Devolución de la venta %1").arg(number)))
                return 0;
        }
    }

    if (r.method == QLatin1String(PaymentMethod::CreditNote)) {
        QSqlQuery note(db());
        note.prepare(QStringLiteral("INSERT INTO notas_credito (devolucion_id, cliente_id, cliente_nombre, monto, saldo)"
                                    " VALUES (?, ?, ?, ?, ?)"));
        note.addBindValue(id);
        note.addBindValue(sale.sale.customerId > 0 ? QVariant(sale.sale.customerId) : QVariant(QVariant::LongLong));
        note.addBindValue(sale.sale.customerName);
        note.addBindValue(amount);
        note.addBindValue(amount);
        if (!exec(note))
            return 0;
        QSqlQuery link(db());
        link.prepare(QStringLiteral("UPDATE devoluciones SET referencia = ? WHERE id = ?"));
        link.addBindValue(AguaraSale::formatCreditNote(note.lastInsertId().toLongLong()));
        link.addBindValue(id);
        if (!exec(link))
            return 0;
    }
    return id;
}

std::optional<ReturnRecord> SqlReturnService::returnRecord(qint64 id) const
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral(
        "SELECT d.id, v.caja_id, v.numero, d.fecha, d.usuario, d.autorizo, d.motivo, d.importe, d.pagado, d.medio,"
        " d.referencia, v.cliente_nombre FROM devoluciones d JOIN ventas v ON v.id = d.venta_id WHERE d.id = ?"));
    q.addBindValue(id);
    if (!exec(q))
        return std::nullopt;
    if (!q.next()) {
        m_lastError = tr("La devolución no existe.");
        return std::nullopt;
    }
    ReturnRecord r;
    r.id = q.value(0).toLongLong();
    r.saleNumber = AguaraSale::formatNumber(q.value(1).toLongLong(), q.value(2).toLongLong());
    r.when = q.value(3).toDateTime();
    r.user = q.value(4).toString();
    r.authorizedBy = q.value(5).toString();
    r.reason = q.value(6).toString();
    r.amount = q.value(7).toLongLong();
    r.paid = q.value(8).toLongLong();
    r.method = q.value(9).toString();
    r.reference = q.value(10).toString();
    r.customerName = q.value(11).toString();

    QSqlQuery items(db());
    items.prepare(QStringLiteral("SELECT producto_codigo, descripcion, unidad, decimales, cantidad, importe, iva"
                                 " FROM devoluciones_items WHERE devolucion_id = ? ORDER BY id"));
    items.addBindValue(id);
    if (!exec(items))
        return std::nullopt;
    while (items.next()) {
        ReturnedItem item;
        item.code = items.value(0).toString();
        item.description = items.value(1).toString();
        item.unit = items.value(2).toString();
        item.decimals = items.value(3).toInt();
        item.quantity = items.value(4).toLongLong();
        item.amount = items.value(5).toLongLong();
        item.vat = items.value(6).toLongLong();
        r.items << item;
    }
    return r;
}

std::optional<CreditNote> SqlReturnService::creditNote(const QString &number) const
{
    const qint64 id = AguaraSale::creditNoteId(number);
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT id, monto, saldo, fecha, cliente_nombre FROM notas_credito WHERE id = ?"));
    q.addBindValue(id);
    if (!exec(q))
        return std::nullopt;
    if (!q.next()) {
        m_lastError = tr("No hay ninguna nota de crédito con ese número.");
        return std::nullopt;
    }
    CreditNote note;
    note.id = q.value(0).toLongLong();
    note.number = AguaraSale::formatCreditNote(note.id);
    note.amount = q.value(1).toLongLong();
    note.balance = q.value(2).toLongLong();
    note.when = q.value(3).toDateTime();
    note.customerName = q.value(4).toString();
    return note;
}
