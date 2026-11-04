#include "sqlsalesservice.h"
#include "connectionmonitor.h"
#include "errors.h"
#include "logging.h"
#include "sqlcashservice.h"
#include "sqlproductservice.h"
#include "stockledger.h"

#include <inventoryservice.h>
#include <userservice.h>

#include <QHash>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

SqlSalesService::SqlSalesService(ConnectionMonitor *monitor, SqlProductService *products, SqlCashService *cash,
                                 QObject *parent)
    : QObject(parent), m_monitor(monitor), m_products(products), m_cash(cash)
    , m_connectionName(monitor->connectionName())
    , m_outbox(monitor->connectionName())
{
    m_cash->setPendingSales([this]() { return m_outbox.open() ? m_outbox.count() : 0; });

    // As soon as the server is back, and every so often for what it refused
    connect(monitor, &ConnectionMonitor::reconnected, this, &SqlSalesService::flushPending, Qt::QueuedConnection);
    connect(&m_retry, &QTimer::timeout, this, &SqlSalesService::flushPending);
    m_retry.start(30000);
}

SqlSalesService::~SqlSalesService()
{
    m_cash->setPendingSales({});
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

/// That the articles exist: this one asks the database, so it waits for push().
bool SqlSalesService::validateArticles(const SaleDraft &draft) const
{
    for (const SaleLine &line : draft.lines) {
        if (!m_products->product(line.code)) {
            m_lastError = tr("El artículo %1 no existe.").arg(line.code);
            return false;
        }
    }
    return true;
}

/// Everything that can be checked with no server.
bool SqlSalesService::validate(const SaleDraft &draft, const SaleTotals &totals) const
{
    if (draft.lines.isEmpty()) {
        m_lastError = tr("La venta no tiene artículos.");
        return false;
    }
    Money afterLineDiscounts = 0;
    for (const SaleLine &line : draft.lines) {
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

    // What came before goes first, and keeps its place in the numbering
    if (online())
        flushPending();
    const bool nothingBefore = online() && m_outbox.open() && m_outbox.count() == 0;

    // First on this computer's disk. Only then the server.
    if (!m_outbox.open()) {
        m_lastError = m_outbox.lastError();
        return 0;
    }
    OutboxEntry entry;
    entry.uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    entry.when = QDateTime::currentDateTime();
    entry.user = userName();
    entry.registerId = session->registerId;
    entry.sessionId = session->id;
    entry.draft = draft;
    entry.payments = payments;
    entry.id = m_outbox.add(entry);
    if (entry.id == 0) {
        m_lastError = m_outbox.lastError();
        return 0;
    }

    if (nothingBefore) {
        qint64 saleId = 0;
        switch (push(entry, &saleId)) {
        case Push::Saved:
            m_outbox.remove(entry.id);
            return saleId;
        case Push::Refused: {
            const QString reason = m_lastError;     // the screen keeps the sale: it was never charged
            m_outbox.remove(entry.id);
            m_lastError = reason;
            return 0;
        }
        case Push::NoServer:
            break;
        }
    }

    // It stays in the queue. The customer leaves with this number on the ticket.
    m_outbox.setNumber(entry.id, localNumber(entry.registerId));
    qCInfo(lcDatabase) << "Sale" << entry.uuid << "stays in the queue";
    QTimer::singleShot(0, this, [this]() { flushPending(); });
    return -entry.id;
}

/// One past everything known: the server's last answer and the queue. This
/// computer is the only one that numbers this register.
qint64 SqlSalesService::localNumber(qint64 registerId)
{
    return qMax(m_cash->lastNumber(), m_outbox.lastNumber(registerId)) + 1;
}

bool SqlSalesService::online() const
{
    return m_monitor->isConnected() && !m_monitor->connectionBroken();
}

/// Why a push failed. A connection that was replaced took the transaction with
/// it, even if the new one works.
SqlSalesService::Push SqlSalesService::failure(int generation)
{
    return generation != m_monitor->generation() || !online() ? Push::NoServer : Push::Refused;
}

/// The number the sale was promised, or the next one if the database gave it
/// to someone else meanwhile: a second copy of the program on this computer.
qint64 SqlSalesService::claimNumber(qint64 registerId, qint64 number)
{
    QSqlQuery taken(db());
    taken.prepare(QStringLiteral("SELECT id FROM ventas WHERE caja_id = ? AND numero = ?"));
    taken.addBindValue(registerId);
    taken.addBindValue(number);
    if (!exec(taken))
        return 0;
    if (taken.next()) {
        qCWarning(lcDatabase) << "Number" << number << "was taken: the queued sale gets another one";
        taken.finish();
        return nextNumber(registerId);
    }
    taken.finish();
    QSqlQuery update(db());
    update.prepare(QStringLiteral("UPDATE cajas SET ultimo_numero = CASE WHEN ultimo_numero < ? THEN ? ELSE ultimo_numero END"
                                  " WHERE id = ?"));
    update.addBindValue(number);
    update.addBindValue(number);
    update.addBindValue(registerId);
    return exec(update) ? number : 0;
}

/// One sale into the database, in one transaction. Safe to repeat: if the
/// connection died during a COMMIT that did reach the server, the sale's uuid
/// finds it.
SqlSalesService::Push SqlSalesService::push(const OutboxEntry &entry, qint64 *saleId)
{
    const int generation = m_monitor->generation();

    QSqlQuery existing(db());
    existing.prepare(QStringLiteral("SELECT id FROM ventas WHERE uuid = ?"));
    existing.addBindValue(entry.uuid);
    if (!exec(existing))
        return failure(generation);
    if (existing.next()) {
        *saleId = existing.value(0).toLongLong();
        return Push::Saved;
    }
    existing.finish();

    SaleTotals totals = AguaraSale::totals(entry.draft);
    QList<SalePayment> settled;
    if (!settle(entry.payments, &totals, &settled))
        return Push::Refused;
    if (!validateArticles(entry.draft))
        return failure(generation);

    // Read before the transaction: costs and how much stock is left
    QHash<QString, Product> articles;
    for (const SaleLine &line : entry.draft.lines)
        articles.insert(line.code, *m_products->product(line.code));

    // Charged without a server, nobody could check its vouchers then
    QStringList repeated;
    for (const SalePayment &p : settled) {
        if (entry.number == 0 || !AguaraSale::isElectronic(p.method))
            continue;
        const QString usedBy = referenceUsedBy(p.reference);
        if (!usedBy.isEmpty())
            repeated << tr("Comprobante %1, ya usado en la venta %2").arg(p.reference, usedBy);
    }

    if (!begin())
        return failure(generation);
    const qint64 number = entry.number > 0 ? claimNumber(entry.registerId, entry.number)
                                           : nextNumber(entry.registerId);
    if (number == 0) {
        rollback();
        return failure(generation);
    }
    const QString printed = AguaraSale::formatNumber(entry.registerId, number);

    QSqlQuery head(db());
    head.prepare(QStringLiteral(
        "INSERT INTO ventas (uuid, caja_id, sesion_id, numero, fecha, usuario, estado, impresiones, bruto, descuento,"
        " neto, redondeo, total, gravado10, iva10, gravado5, iva5, exento)"
        " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    head.addBindValue(entry.uuid);
    head.addBindValue(entry.registerId);
    head.addBindValue(entry.sessionId);
    head.addBindValue(number);
    head.addBindValue(entry.when);
    head.addBindValue(entry.user);
    head.addBindValue(QString::fromLatin1(SaleState::Paid));
    head.addBindValue(entry.printCount);
    for (Money value : { totals.gross, totals.discount, totals.net, totals.rounding, totals.total,
                         totals.taxed10, totals.vat10, totals.taxed5, totals.vat5, totals.exempt })
        head.addBindValue(value);
    if (!exec(head)) {
        rollback();
        return failure(generation);
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
            return failure(generation);
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
            return failure(generation);
        }
    }
    if (!commit())
        return failure(generation);

    *saleId = id;
    m_cash->noteNumber(number);

    // Allowed on purpose, and on record: late purchases must not stop a sale
    for (auto it = sold.cbegin(); it != sold.cend(); ++it) {
        const Product &p = articles[it.key()];
        if (p.stock - it.value() < 0)
            audit(QStringLiteral("venta.stock_negativo"), printed,
                  tr("%1: queda %2").arg(p.description, AguaraQuantity::format(p.stock - it.value(), p.unit.decimals)));
    }
    if (entry.number > 0)
        audit(QStringLiteral("venta.diferida"), printed, tr("Cobrada el %1, cajero %2")
              .arg(entry.when.toString(QStringLiteral("dd/MM/yyyy HH:mm")), entry.user));
    for (const QString &text : qAsConst(repeated))
        audit(QStringLiteral("venta.comprobante_repetido"), printed, text);
    m_products->refreshAfterChange();
    return Push::Saved;
}

bool SqlSalesService::flushPending()
{
    if (m_flushing || !m_outbox.open())
        return false;
    if (m_outbox.count() == 0)
        return true;
    if (!online())
        return false;

    m_flushing = true;
    for (const OutboxEntry &entry : m_outbox.entries()) {
        qint64 saleId = 0;
        const Push result = push(entry, &saleId);
        if (result == Push::NoServer)
            break;                      // it all waits for the next connection
        if (result == Push::Saved) {
            m_outbox.remove(entry.id);
            qCInfo(lcDatabase) << "Queued sale" << entry.uuid << "reached the database";
        } else {                        // the others go on: this one stays, and shows
            qCCritical(lcDatabase).noquote() << "Queued sale" << entry.uuid << "refused:" << m_lastError;
            m_outbox.setError(entry.id, m_lastError);
        }
    }
    m_flushing = false;
    return m_outbox.count() == 0;
}

QList<PendingSale> SqlSalesService::pendingSales() const
{
    QList<PendingSale> result;
    if (!m_outbox.isOpen())
        return result;
    for (const OutboxEntry &entry : m_outbox.entries()) {
        SaleTotals totals = AguaraSale::totals(entry.draft);
        QList<SalePayment> settled;
        settle(entry.payments, &totals, &settled);
        PendingSale p;
        p.registerId = entry.registerId;
        p.number = entry.number;
        p.when = entry.when;
        p.user = entry.user;
        p.total = totals.total;
        p.error = entry.error;
        result << p;
    }
    return result;
}

/// A sale still in the queue, as sale() gives one from the database.
std::optional<SaleRecord> SqlSalesService::queuedSale(qint64 id) const
{
    const std::optional<OutboxEntry> entry = m_outbox.entry(id);
    if (!entry) {
        m_lastError = tr("La venta no existe.");
        return std::nullopt;
    }
    SaleRecord record;
    record.id = -entry->id;
    record.registerId = entry->registerId;
    record.sessionId = entry->sessionId;
    record.number = entry->number;
    record.when = entry->when;
    record.user = entry->user;
    record.state = QString::fromLatin1(SaleState::Paid);
    record.printCount = entry->printCount;
    record.totals = AguaraSale::totals(entry->draft);
    if (!settle(entry->payments, &record.totals, &record.payments))
        return std::nullopt;
    return record;
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
        " gravado10, iva10, gravado5, iva5, exento, anulada_por, anulada_fecha, motivo, impresiones"
        " FROM ventas ") + where);
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
        s.printCount = q.value(20).toInt();
        result << s;
    }
    return result;
}

std::optional<SaleRecord> SqlSalesService::sale(qint64 id) const
{
    if (id < 0)
        return queuedSale(-id);
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
    if (id < 0) {
        m_lastError = tr("La venta todavía no llegó al servidor: se anula cuando llegue.");
        return false;
    }
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

bool SqlSalesService::recordPrint(qint64 id)
{
    if (id < 0)
        return m_outbox.addPrint(-id);
    QSqlQuery q(db());
    q.prepare(QStringLiteral("UPDATE ventas SET impresiones = impresiones + 1 WHERE id = ?"));
    q.addBindValue(id);
    return exec(q);
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
    q.addBindValue(QString::fromUtf8(SaleJson::fromDraft(draft)));
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
        h.draft = SaleJson::toDraft(q.value(3).toString().toUtf8());
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
