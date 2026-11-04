#include "sqlsalesservice.h"
#include "connectionmonitor.h"
#include "errors.h"
#include "sqlcashservice.h"
#include "sqlproductservice.h"
#include "stockledger.h"

#include <inventoryservice.h>
#include <userservice.h>

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace {

/// Money and quantities go as text: JSON numbers are doubles.
QByteArray draftToJson(const SaleDraft &draft)
{
    QJsonArray lines;
    for (const SaleLine &l : draft.lines) {
        QJsonObject o;
        o[QStringLiteral("code")] = l.code;
        o[QStringLiteral("description")] = l.description;
        o[QStringLiteral("unit")] = l.unit;
        o[QStringLiteral("decimals")] = l.decimals;
        o[QStringLiteral("quantity")] = QString::number(l.quantity);
        o[QStringLiteral("unitPrice")] = QString::number(l.unitPrice);
        o[QStringLiteral("fixedAmount")] = QString::number(l.fixedAmount);
        o[QStringLiteral("discount")] = QString::number(l.discount);
        o[QStringLiteral("vatRate")] = l.vatRate;
        lines.append(o);
    }
    QJsonObject root;
    root[QStringLiteral("lines")] = lines;
    root[QStringLiteral("totalDiscount")] = QString::number(draft.totalDiscount);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

SaleDraft draftFromJson(const QByteArray &json)
{
    SaleDraft draft;
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    draft.totalDiscount = root.value(QStringLiteral("totalDiscount")).toString().toLongLong();
    for (const QJsonValue &value : root.value(QStringLiteral("lines")).toArray()) {
        const QJsonObject o = value.toObject();
        SaleLine l;
        l.code = o.value(QStringLiteral("code")).toString();
        l.description = o.value(QStringLiteral("description")).toString();
        l.unit = o.value(QStringLiteral("unit")).toString();
        l.decimals = o.value(QStringLiteral("decimals")).toInt();
        l.quantity = o.value(QStringLiteral("quantity")).toString().toLongLong();
        l.unitPrice = o.value(QStringLiteral("unitPrice")).toString().toLongLong();
        l.fixedAmount = o.value(QStringLiteral("fixedAmount")).toString().toLongLong();
        l.discount = o.value(QStringLiteral("discount")).toString().toLongLong();
        l.vatRate = o.value(QStringLiteral("vatRate")).toInt();
        draft.lines << l;
    }
    return draft;
}

} // namespace

SqlSalesService::SqlSalesService(ConnectionMonitor *monitor, SqlProductService *products, SqlCashService *cash,
                                 QObject *parent)
    : QObject(parent), m_monitor(monitor), m_products(products), m_cash(cash)
    , m_connectionName(monitor->connectionName())
{
}

QSqlDatabase SqlSalesService::db() const
{
    return QSqlDatabase::database(m_connectionName, false);
}

bool SqlSalesService::exec(QSqlQuery &q) const
{
    if (m_monitor->exec(q, m_inTransaction))
        return true;
    m_lastError = m_monitor->lastError();
    return false;
}

bool SqlSalesService::begin()
{
    if (m_monitor->connectionBroken())
        m_monitor->reconnect();
    if (!db().transaction()) {
        m_lastError = AguaraErrors::logAndExplain(db().lastError(), QStringLiteral("BEGIN"));
        return false;
    }
    m_inTransaction = true;
    return true;
}

bool SqlSalesService::commit()
{
    m_inTransaction = false;
    QSqlDatabase database = db();
    if (database.commit())
        return true;
    m_lastError = AguaraErrors::logAndExplain(database.lastError(), QStringLiteral("COMMIT"));
    database.rollback();
    return false;
}

void SqlSalesService::rollback()
{
    m_inTransaction = false;
    db().rollback();
}

void SqlSalesService::audit(const QString &action, const QString &entityId, const QString &after)
{
    if (m_users)
        m_users->audit(action, QStringLiteral("venta"), entityId, QString(), after);
}

QString SqlSalesService::userName() const
{
    const auto user = m_users ? m_users->currentUser() : std::nullopt;
    return user ? user->username : QString();
}

bool SqlSalesService::validate(const SaleDraft &draft, const SaleTotals &totals) const
{
    if (draft.lines.isEmpty()) {
        m_lastError = tr("La venta no tiene artículos.");
        return false;
    }
    Money afterLineDiscounts = 0;
    for (const SaleLine &line : draft.lines) {
        const std::optional<Product> p = m_products->product(line.code);
        if (!p) {
            m_lastError = tr("El artículo %1 no existe.").arg(line.code);
            return false;
        }
        const Money gross = AguaraSale::lineGross(line);
        if (line.quantity <= 0 || line.unitPrice < 0) {
            m_lastError = tr("Revise la cantidad y el precio de %1.").arg(line.description);
            return false;
        }
        if (line.discount < 0 || line.discount > gross) {
            m_lastError = tr("El descuento de %1 no es válido.").arg(line.description);
            return false;
        }
        afterLineDiscounts += gross - line.discount;
    }
    if (draft.totalDiscount < 0 || draft.totalDiscount > afterLineDiscounts) {
        m_lastError = tr("El descuento de la venta no es válido.");
        return false;
    }
    Money sum = 0;
    for (const SaleItem &item : totals.items)
        sum += item.net;
    if (sum != totals.net) {                    // a programming error, never a sale with bad totals
        m_lastError = tr("Los totales de la venta no coinciden.");
        return false;
    }
    return true;
}

/// Inside the sale's transaction. The UPDATE locks the register's row, and a
/// failed sale gives its number back: no holes and no repeats, on both
/// databases.
qint64 SqlSalesService::nextNumber(qint64 registerId)
{
    QSqlQuery update(db());
    update.prepare(QStringLiteral("UPDATE cajas SET ultimo_numero = ultimo_numero + 1 WHERE id = ?"));
    update.addBindValue(registerId);
    if (!exec(update))
        return 0;
    QSqlQuery read(db());
    read.prepare(QStringLiteral("SELECT ultimo_numero FROM cajas WHERE id = ?"));
    read.addBindValue(registerId);
    if (!exec(read) || !read.next())
        return 0;
    return read.value(0).toLongLong();
}

/// Checks the payments against the sale and works out the cash: its rounding
/// and its change. `settled` is what gets saved.
bool SqlSalesService::settle(const QList<SalePayment> &payments, SaleTotals *totals,
                             QList<SalePayment> *settled) const
{
    Money electronic = 0;
    std::optional<SalePayment> cash;
    QSet<QString> references;
    for (SalePayment p : payments) {
        if (p.amount <= 0) {
            m_lastError = tr("Revise los montos del pago.");
            return false;
        }
        if (p.method == QLatin1String(PaymentMethod::Cash)) {
            if (cash) {
                m_lastError = tr("El efectivo va en un solo pago.");
                return false;
            }
            cash = p;
            continue;
        }
        if (!AguaraSale::isElectronic(p.method)) {
            m_lastError = tr("El medio de pago %1 no existe.").arg(p.method);
            return false;
        }
        p.reference = AguaraSale::normalizeReference(p.reference);
        if (p.reference.isEmpty()) {
            m_lastError = tr("Falta el código del comprobante.");
            return false;
        }
        if (references.contains(p.reference)) {
            m_lastError = tr("El comprobante %1 está dos veces en esta venta.").arg(p.reference);
            return false;
        }
        references.insert(p.reference);
        p.change = 0;
        electronic += p.amount;
        settled->append(p);
    }
    if (electronic > totals->net) {
        m_lastError = tr("Los pagos superan el total de Gs. %1, y tarjetas y transferencias no dan vuelto.")
                          .arg(AguaraMoney::format(totals->net));
        return false;
    }

    const Money due = AguaraSale::cashDue(totals->net, electronic);
    const Money tendered = cash ? cash->amount : 0;
    if (tendered < due) {
        m_lastError = tr("Falta dinero: quedan Gs. %1 por pagar en efectivo.").arg(AguaraMoney::format(due));
        return false;
    }
    if (cash && due == 0) {
        m_lastError = tr("No queda nada por pagar en efectivo.");
        return false;
    }
    if (cash) {
        cash->change = tendered - due;
        cash->reference.clear();
        settled->append(*cash);
    }
    totals->total = electronic + due;
    totals->rounding = totals->total - totals->net;
    return true;
}

qint64 SqlSalesService::checkout(const SaleDraft &draft, const QList<SalePayment> &payments)
{
    const std::optional<CashSession> session = m_cash->currentSession();
    if (!session) {
        m_lastError = tr("La caja está cerrada: ábrala para vender.");
        return 0;
    }
    if (session->isStale()) {
        m_lastError = tr("La sesión abierta es de otro día: hay que cerrarla antes de vender.");
        return 0;
    }
    SaleTotals totals = AguaraSale::totals(draft);
    if (!validate(draft, totals))
        return 0;
    QList<SalePayment> settled;
    if (!settle(payments, &totals, &settled))
        return 0;

    // Read before the transaction: costs and how much stock is left
    QHash<QString, Product> articles;
    for (const SaleLine &line : draft.lines)
        articles.insert(line.code, *m_products->product(line.code));

    if (!begin())
        return 0;
    const qint64 number = nextNumber(session->registerId);
    if (number == 0) {
        rollback();
        return 0;
    }
    const QString printed = AguaraSale::formatNumber(session->registerId, number);
    const QString user = userName();

    QSqlQuery head(db());
    head.prepare(QStringLiteral(
        "INSERT INTO ventas (caja_id, sesion_id, numero, usuario, estado, bruto, descuento, neto, redondeo, total,"
        " gravado10, iva10, gravado5, iva5, exento) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    head.addBindValue(session->registerId);
    head.addBindValue(session->id);
    head.addBindValue(number);
    head.addBindValue(user);
    head.addBindValue(QString::fromLatin1(SaleState::Paid));
    for (Money value : { totals.gross, totals.discount, totals.net, totals.rounding, totals.total,
                         totals.taxed10, totals.vat10, totals.taxed5, totals.vat5, totals.exempt })
        head.addBindValue(value);
    if (!exec(head)) {
        rollback();
        return 0;
    }
    const qint64 id = head.lastInsertId().toLongLong();

    StockLedger ledger(m_monitor, m_connectionName, &m_lastError, m_users);
    QHash<QString, Quantity> sold;
    int position = 0;
    for (const SaleItem &item : totals.items) {
        const Money cost = articles.value(item.code).cost;
        QSqlQuery line(db());
        line.prepare(QStringLiteral(
            "INSERT INTO ventas_items (venta_id, linea, producto_codigo, descripcion, unidad, decimales, cantidad,"
            " precio_unitario, bruto, descuento, importe, iva_tasa, iva, costo_unitario)"
            " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
        line.addBindValue(id);
        line.addBindValue(++position);
        line.addBindValue(item.code);
        line.addBindValue(item.description);
        line.addBindValue(item.unit);
        line.addBindValue(item.decimals);
        line.addBindValue(item.quantity);
        line.addBindValue(item.unitPrice);
        line.addBindValue(item.gross);
        line.addBindValue(item.discount);
        line.addBindValue(item.net);
        line.addBindValue(item.vatRate);
        line.addBindValue(item.vat);
        line.addBindValue(cost);
        if (!exec(line) || !ledger.move(item.code, StockMovementType::Sale, -item.quantity, cost, QString(),
                                        tr("Venta %1").arg(printed))) {
            rollback();
            return 0;
        }
        sold[item.code] += item.quantity;
    }

    for (const SalePayment &p : settled) {
        QSqlQuery payment(db());
        payment.prepare(QStringLiteral(
            "INSERT INTO ventas_pagos (venta_id, medio, monto, vuelto, referencia) VALUES (?, ?, ?, ?, ?)"));
        payment.addBindValue(id);
        payment.addBindValue(p.method);
        payment.addBindValue(p.amount);
        payment.addBindValue(p.change);
        payment.addBindValue(p.reference.isEmpty() ? QVariant(QVariant::String) : QVariant(p.reference));
        if (!exec(payment)) {
            rollback();
            return 0;
        }
    }
    if (!commit())
        return 0;

    // Allowed on purpose, and on record: late purchases must not stop a sale
    for (auto it = sold.cbegin(); it != sold.cend(); ++it) {
        const Product &p = articles[it.key()];
        if (p.stock - it.value() < 0)
            audit(QStringLiteral("venta.stock_negativo"), printed,
                  tr("%1: queda %2").arg(p.description, AguaraQuantity::format(p.stock - it.value(), p.unit.decimals)));
    }
    m_products->refreshAfterChange();
    return id;
}

QString SqlSalesService::referenceUsedBy(const QString &reference) const
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral(
        "SELECT v.caja_id, v.numero FROM ventas_pagos p JOIN ventas v ON v.id = p.venta_id"
        " WHERE p.referencia = ? AND v.estado = ? ORDER BY v.id DESC"));
    q.addBindValue(AguaraSale::normalizeReference(reference));
    q.addBindValue(QString::fromLatin1(SaleState::Paid));
    if (!exec(q) || !q.next())
        return QString();
    return AguaraSale::formatNumber(q.value(0).toLongLong(), q.value(1).toLongLong());
}

QList<SaleRecord> SqlSalesService::readSales(const QString &where, const QVariantList &values) const
{
    QList<SaleRecord> result;
    QSqlQuery q(db());
    q.prepare(QStringLiteral(
        "SELECT id, caja_id, sesion_id, numero, fecha, usuario, estado, bruto, descuento, neto, redondeo, total,"
        " gravado10, iva10, gravado5, iva5, exento, anulada_por, anulada_fecha, motivo FROM ventas ") + where);
    for (const QVariant &value : values)
        q.addBindValue(value);
    if (!exec(q))
        return result;
    while (q.next()) {
        SaleRecord s;
        s.id = q.value(0).toLongLong();
        s.registerId = q.value(1).toLongLong();
        s.sessionId = q.value(2).toLongLong();
        s.number = q.value(3).toLongLong();
        s.when = q.value(4).toDateTime();
        s.user = q.value(5).toString();
        s.state = q.value(6).toString();
        s.totals.gross = q.value(7).toLongLong();
        s.totals.discount = q.value(8).toLongLong();
        s.totals.net = q.value(9).toLongLong();
        s.totals.rounding = q.value(10).toLongLong();
        s.totals.total = q.value(11).toLongLong();
        s.totals.taxed10 = q.value(12).toLongLong();
        s.totals.vat10 = q.value(13).toLongLong();
        s.totals.taxed5 = q.value(14).toLongLong();
        s.totals.vat5 = q.value(15).toLongLong();
        s.totals.exempt = q.value(16).toLongLong();
        s.voidedBy = q.value(17).toString();
        s.voidedAt = q.value(18).toDateTime();
        s.voidReason = q.value(19).toString();
        result << s;
    }
    return result;
}

std::optional<SaleRecord> SqlSalesService::sale(qint64 id) const
{
    const QList<SaleRecord> found = readSales(QStringLiteral("WHERE id = ?"), { id });
    if (found.isEmpty()) {
        m_lastError = tr("La venta no existe.");
        return std::nullopt;
    }
    SaleRecord record = found.first();

    QSqlQuery items(db());
    items.prepare(QStringLiteral(
        "SELECT producto_codigo, descripcion, unidad, decimales, cantidad, precio_unitario, bruto, descuento, importe,"
        " iva_tasa, iva FROM ventas_items WHERE venta_id = ? ORDER BY linea"));
    items.addBindValue(id);
    if (!exec(items))
        return std::nullopt;
    while (items.next()) {
        SaleItem item;
        item.code = items.value(0).toString();
        item.description = items.value(1).toString();
        item.unit = items.value(2).toString();
        item.decimals = items.value(3).toInt();
        item.quantity = items.value(4).toLongLong();
        item.unitPrice = items.value(5).toLongLong();
        item.gross = items.value(6).toLongLong();
        item.discount = items.value(7).toLongLong();
        item.net = items.value(8).toLongLong();
        item.vatRate = items.value(9).toInt();
        item.vat = items.value(10).toLongLong();
        record.totals.items << item;
    }

    QSqlQuery payments(db());
    payments.prepare(QStringLiteral("SELECT medio, monto, vuelto, referencia FROM ventas_pagos WHERE venta_id = ? ORDER BY id"));
    payments.addBindValue(id);
    if (!exec(payments))
        return std::nullopt;
    while (payments.next()) {
        SalePayment p;
        p.method = payments.value(0).toString();
        p.amount = payments.value(1).toLongLong();
        p.change = payments.value(2).toLongLong();
        p.reference = payments.value(3).toString();
        record.payments << p;
    }
    return record;
}

QList<SaleRecord> SqlSalesService::sessionSales(qint64 sessionId) const
{
    return readSales(QStringLiteral("WHERE sesion_id = ? ORDER BY id DESC"), { sessionId });
}

bool SqlSalesService::voidSale(qint64 id, const QString &reason)
{
    if (reason.trimmed().isEmpty()) {
        m_lastError = tr("Indique el motivo de la anulación.");
        return false;
    }
    const std::optional<SaleRecord> record = sale(id);
    if (!record)
        return false;
    if (record->state != QLatin1String(SaleState::Paid)) {
        m_lastError = tr("La venta ya está anulada.");
        return false;
    }
    const std::optional<CashSession> session = m_cash->currentSession();
    if (!session || session->id != record->sessionId) {
        m_lastError = tr("Solo se puede anular una venta de la sesión de caja actual.");
        return false;
    }

    const QString printed = AguaraSale::formatNumber(record->registerId, record->number);
    if (!begin())
        return false;
    QSqlQuery q(db());
    q.prepare(QStringLiteral("UPDATE ventas SET estado = ?, anulada_por = ?, anulada_fecha = ?, motivo = ?"
                             " WHERE id = ? AND estado = ?"));
    q.addBindValue(QString::fromLatin1(SaleState::Voided));
    q.addBindValue(userName());
    q.addBindValue(QDateTime::currentDateTime());
    q.addBindValue(reason.trimmed());
    q.addBindValue(id);
    q.addBindValue(QString::fromLatin1(SaleState::Paid));
    if (!exec(q)) {
        rollback();
        return false;
    }
    if (q.numRowsAffected() != 1) {
        m_lastError = tr("La venta ya está anulada.");
        rollback();
        return false;
    }
    StockLedger ledger(m_monitor, m_connectionName, &m_lastError, m_users);
    for (const SaleItem &item : record->totals.items) {
        const std::optional<Product> p = m_products->product(item.code);
        if (!ledger.move(item.code, StockMovementType::Return, item.quantity, p ? p->cost : 0, reason.trimmed(),
                         tr("Anulación de la venta %1").arg(printed))) {
            rollback();
            return false;
        }
    }
    if (!commit())
        return false;
    audit(QStringLiteral("venta.anular"), printed,
          tr("Gs. %1: %2").arg(AguaraMoney::format(record->totals.total), reason.trimmed()));
    m_products->refreshAfterChange();
    return true;
}

qint64 SqlSalesService::holdSale(const SaleDraft &draft)
{
    if (draft.lines.isEmpty()) {
        m_lastError = tr("La venta no tiene artículos.");
        return 0;
    }
    const CashRegister cashRegister = m_cash->currentRegister();
    if (cashRegister.id == 0)
        return 0;
    QSqlQuery q(db());
    q.prepare(QStringLiteral("INSERT INTO ventas_espera (caja_id, usuario, datos) VALUES (?, ?, ?)"));
    q.addBindValue(cashRegister.id);
    q.addBindValue(userName());
    q.addBindValue(QString::fromUtf8(draftToJson(draft)));
    if (!exec(q))
        return 0;
    return q.lastInsertId().toLongLong();
}

QList<HeldSale> SqlSalesService::heldSales() const
{
    QList<HeldSale> result;
    const CashRegister cashRegister = m_cash->currentRegister();
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT id, fecha, usuario, datos FROM ventas_espera WHERE caja_id = ? ORDER BY id"));
    q.addBindValue(cashRegister.id);
    if (!exec(q))
        return result;
    while (q.next()) {
        HeldSale h;
        h.id = q.value(0).toLongLong();
        h.when = q.value(1).toDateTime();
        h.user = q.value(2).toString();
        h.draft = draftFromJson(q.value(3).toString().toUtf8());
        result << h;
    }
    return result;
}

std::optional<SaleDraft> SqlSalesService::takeHeldSale(qint64 id)
{
    for (const HeldSale &held : heldSales()) {
        if (held.id != id)
            continue;
        QSqlQuery q(db());
        q.prepare(QStringLiteral("DELETE FROM ventas_espera WHERE id = ?"));
        q.addBindValue(id);
        if (!exec(q))
            return std::nullopt;
        return held.draft;
    }
    m_lastError = tr("Esa venta ya no está en espera.");
    return std::nullopt;
}
