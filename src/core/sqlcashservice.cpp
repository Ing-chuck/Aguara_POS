#include "sqlcashservice.h"
#include "connectionmonitor.h"
#include "errors.h"

#include <salesservice.h>
#include <userservice.h>

#include <QSqlError>
#include <QSqlQuery>
#include <QSysInfo>
#include <QVariant>

namespace {
constexpr const char *CountOpening = "apertura";
constexpr const char *CountClosing = "cierre";
}

SqlCashService::SqlCashService(ConnectionMonitor *monitor, const QString &station, QObject *parent)
    : QObject(parent), m_monitor(monitor), m_connectionName(monitor->connectionName())
    , m_station(station.isEmpty() ? QSysInfo::machineHostName() : station)
{
}

QSqlDatabase SqlCashService::db() const
{
    return QSqlDatabase::database(m_connectionName, false);
}

bool SqlCashService::exec(QSqlQuery &q) const
{
    if (m_monitor->exec(q, m_inTransaction))
        return true;
    m_lastError = m_monitor->lastError();
    return false;
}

bool SqlCashService::begin()
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

bool SqlCashService::commit()
{
    m_inTransaction = false;
    QSqlDatabase database = db();
    if (database.commit())
        return true;
    m_lastError = AguaraErrors::logAndExplain(database.lastError(), QStringLiteral("COMMIT"));
    database.rollback();
    return false;
}

void SqlCashService::rollback()
{
    m_inTransaction = false;
    db().rollback();
}

void SqlCashService::audit(const QString &action, const QString &entityId, const QString &after)
{
    if (m_users)
        m_users->audit(action, QStringLiteral("caja"), entityId, QString(), after);
}

QString SqlCashService::userName() const
{
    const auto user = m_users ? m_users->currentUser() : std::nullopt;
    return user ? user->username : QString();
}

// --- registers ---------------------------------------------------------------

CashRegister SqlCashService::currentRegister()
{
    if (!m_monitor->isConnected() && m_knownRegister.id != 0)
        return m_knownRegister;
    CashRegister result;
    result.station = m_station;
    QSqlQuery find(db());
    find.prepare(QStringLiteral("SELECT id, nombre FROM cajas WHERE puesto = ?"));
    find.addBindValue(m_station);
    if (!exec(find))
        return m_monitor->isConnected() ? result : m_knownRegister;
    if (find.next()) {
        result.id = find.value(0).toLongLong();
        result.name = find.value(1).toString();
        m_knownRegister = result;
        return result;
    }
    find.finish();

    // Two computers starting together can't take the same station; the name
    // is only a suggestion, the user can change it.
    QSqlQuery count(db());
    if (!m_monitor->exec(count, QStringLiteral("SELECT COUNT(*) FROM cajas")) || !count.next()) {
        m_lastError = m_monitor->lastError();
        return result;
    }
    const QString name = tr("Caja %1").arg(count.value(0).toLongLong() + 1);
    count.finish();
    QSqlQuery add(db());
    add.prepare(QStringLiteral("INSERT INTO cajas (nombre, puesto) VALUES (?, ?)"));
    add.addBindValue(name);
    add.addBindValue(m_station);
    if (!exec(add))
        return result;
    result.id = add.lastInsertId().toLongLong();
    result.name = name;
    audit(QStringLiteral("caja.crear"), QString::number(result.id), name + QStringLiteral(" en ") + m_station);
    return result;
}

bool SqlCashService::renameRegister(const QString &name)
{
    const QString text = name.trimmed();
    if (text.isEmpty()) {
        m_lastError = tr("Falta el nombre de la caja.");
        return false;
    }
    const CashRegister current = currentRegister();
    if (current.id == 0)
        return false;
    QSqlQuery q(db());
    q.prepare(QStringLiteral("UPDATE cajas SET nombre = ? WHERE id = ?"));
    q.addBindValue(text);
    q.addBindValue(current.id);
    if (!exec(q))
        return false;
    audit(QStringLiteral("caja.renombrar"), QString::number(current.id), text);
    return true;
}

// --- sessions ----------------------------------------------------------------

QList<CashSession> SqlCashService::readSessions(const QString &where, const QVariantList &values) const
{
    QList<CashSession> result;
    QSqlQuery q(db());
    q.prepare(QStringLiteral(
        "SELECT s.id, s.caja_id, c.nombre, s.apertura, s.usuario_apertura, s.fondo, s.estado, s.cierre,"
        " s.usuario_cierre, s.esperado, s.contado, s.diferencia, s.motivo, s.autorizado_por"
        " FROM sesiones_caja s JOIN cajas c ON c.id = s.caja_id ") + where);
    for (const QVariant &value : values)
        q.addBindValue(value);
    if (!exec(q))
        return result;
    while (q.next()) {
        CashSession s;
        s.id = q.value(0).toLongLong();
        s.registerId = q.value(1).toLongLong();
        s.registerName = q.value(2).toString();
        s.opened = q.value(3).toDateTime();
        s.openedBy = q.value(4).toString();
        s.openingFloat = q.value(5).toLongLong();
        s.open = q.value(6).toString() == QLatin1String("abierta");
        s.closed = q.value(7).toDateTime();
        s.closedBy = q.value(8).toString();
        s.expected = q.value(9).toLongLong();
        s.counted = q.value(10).toLongLong();
        s.difference = q.value(11).toLongLong();
        s.differenceReason = q.value(12).toString();
        s.authorizedBy = q.value(13).toString();
        result << s;
    }
    return result;
}

std::optional<CashSession> SqlCashService::currentSession()
{
    const CashRegister cashRegister = currentRegister();
    if (!m_monitor->isConnected())
        return m_knownSession;
    if (cashRegister.id == 0)
        return std::nullopt;
    const QList<CashSession> open = readSessions(QStringLiteral("WHERE s.caja_id = ? AND s.estado = 'abierta'"),
                                                 { cashRegister.id });
    if (!m_monitor->isConnected())          // that was the query that found out
        return m_knownSession;

    QSqlQuery number(db());
    number.prepare(QStringLiteral("SELECT ultimo_numero FROM cajas WHERE id = ?"));
    number.addBindValue(cashRegister.id);
    if (exec(number) && number.next())
        m_lastNumber = number.value(0).toLongLong();

    m_knownSession = std::nullopt;
    if (!open.isEmpty())
        m_knownSession = open.first();
    return m_knownSession;
}

/// A sale still on its way isn't in the expected cash yet.
bool SqlCashService::salesArePending()
{
    const int pending = m_pendingSales ? m_pendingSales() : 0;
    if (pending > 0)
        m_lastError = tr("Hay %n venta(s) sin subir al servidor: la caja no se puede cerrar hasta que lleguen. "
                         "Ver Caja > Ventas por subir.", nullptr, pending);
    return pending > 0;
}

CashCount SqlCashService::readCount(qint64 sessionId, const char *moment) const
{
    CashCount result;
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT denominacion, cantidad FROM sesion_conteos WHERE sesion_id = ? AND momento = ?"
                             " ORDER BY denominacion DESC"));
    q.addBindValue(sessionId);
    q.addBindValue(QString::fromLatin1(moment));
    if (!exec(q))
        return result;
    while (q.next()) {
        DenominationCount d;
        d.denomination = q.value(0).toLongLong();
        d.count = q.value(1).toInt();
        result << d;
    }
    return result;
}

bool SqlCashService::writeCount(qint64 sessionId, const char *moment, const CashCount &count)
{
    for (const DenominationCount &d : count) {
        if (d.count == 0)
            continue;
        QSqlQuery q(db());
        q.prepare(QStringLiteral("INSERT INTO sesion_conteos (sesion_id, momento, denominacion, cantidad) VALUES (?, ?, ?, ?)"));
        q.addBindValue(sessionId);
        q.addBindValue(QString::fromLatin1(moment));
        q.addBindValue(d.denomination);
        q.addBindValue(d.count);
        if (!exec(q))
            return false;
    }
    return true;
}

bool SqlCashService::openSession(const CashCount &count)
{
    for (const DenominationCount &d : count) {
        if (d.count < 0) {
            m_lastError = tr("El conteo no puede ser negativo.");
            return false;
        }
    }
    const CashRegister cashRegister = currentRegister();
    if (cashRegister.id == 0)
        return false;
    if (currentSession()) {
        m_lastError = tr("%1 ya tiene una sesión abierta.").arg(cashRegister.name);
        return false;
    }

    const Money openingFloat = AguaraCash::countTotal(count);
    if (!begin())
        return false;
    QSqlQuery q(db());
    q.prepare(QStringLiteral("INSERT INTO sesiones_caja (caja_id, usuario_apertura, fondo) VALUES (?, ?, ?)"));
    q.addBindValue(cashRegister.id);
    q.addBindValue(userName());
    q.addBindValue(openingFloat);
    if (!exec(q)) {
        rollback();
        return false;
    }
    const qint64 id = q.lastInsertId().toLongLong();
    if (!writeCount(id, CountOpening, count)) {
        rollback();
        return false;
    }
    if (!commit())
        return false;
    currentSession();           // remembered: the connection could drop at any moment
    audit(QStringLiteral("caja.abrir"), QString::number(id),
          tr("%1, fondo Gs. %2").arg(cashRegister.name, AguaraMoney::format(openingFloat)));
    return true;
}

bool SqlCashService::addMovement(const char *type, Money amount, const QString &reason)
{
    const QString kind = QString::fromLatin1(type);
    if (kind != QLatin1String(CashMovementType::In) && kind != QLatin1String(CashMovementType::Out)
        && kind != QLatin1String(CashMovementType::Withdrawal)) {
        m_lastError = tr("Tipo de movimiento desconocido.");
        return false;
    }
    if (amount <= 0) {
        m_lastError = tr("El monto debe ser mayor que cero.");
        return false;
    }
    if (reason.trimmed().isEmpty()) {
        m_lastError = tr("Indique el motivo del movimiento.");
        return false;
    }
    const std::optional<CashSession> session = currentSession();
    if (!session) {
        m_lastError = tr("La caja no está abierta.");
        return false;
    }
    QSqlQuery q(db());
    q.prepare(QStringLiteral("INSERT INTO caja_movimientos (sesion_id, tipo, monto, motivo, usuario) VALUES (?, ?, ?, ?, ?)"));
    q.addBindValue(session->id);
    q.addBindValue(kind);
    q.addBindValue(amount);
    q.addBindValue(reason.trimmed());
    q.addBindValue(userName());
    if (!exec(q))
        return false;
    audit(QStringLiteral("caja.movimiento"), QString::number(session->id),
          tr("%1 Gs. %2: %3").arg(kind, AguaraMoney::format(amount), reason.trimmed()));
    return true;
}

QList<CashMovement> SqlCashService::movements(qint64 sessionId) const
{
    QList<CashMovement> result;
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT id, fecha, tipo, monto, motivo, usuario FROM caja_movimientos"
                             " WHERE sesion_id = ? ORDER BY id"));
    q.addBindValue(sessionId);
    if (!exec(q))
        return result;
    while (q.next()) {
        CashMovement m;
        m.id = q.value(0).toLongLong();
        m.when = q.value(1).toDateTime();
        m.type = q.value(2).toString();
        m.amount = q.value(3).toLongLong();
        m.reason = q.value(4).toString();
        m.user = q.value(5).toString();
        result << m;
    }
    return result;
}

/// float + cash sales - change +- movements. Voided sales don't count.
Money SqlCashService::expectedCash(qint64 sessionId, Money openingFloat) const
{
    Money expected = openingFloat;
    QSqlQuery sales(db());
    sales.prepare(QStringLiteral(
        "SELECT CAST(COALESCE(SUM(p.monto - p.vuelto), 0) AS BIGINT) FROM ventas_pagos p"
        " JOIN ventas v ON v.id = p.venta_id WHERE v.sesion_id = ? AND v.estado = ? AND p.medio = ?"));
    sales.addBindValue(sessionId);
    sales.addBindValue(QString::fromLatin1(SaleState::Paid));
    sales.addBindValue(QString::fromLatin1(PaymentMethod::Cash));
    if (exec(sales) && sales.next())
        expected += sales.value(0).toLongLong();
    for (const CashMovement &m : movements(sessionId))
        expected += m.type == QLatin1String(CashMovementType::In) ? m.amount : -m.amount;
    return expected;
}

bool SqlCashService::finish(const CashSession &session, const CashCount &count, Money expected,
                            const QString &reason, const QString &authorizedBy)
{
    const Money counted = AguaraCash::countTotal(count);
    if (!begin())
        return false;
    QSqlQuery q(db());
    // estado in the WHERE: a second close finds nothing to change
    q.prepare(QStringLiteral(
        "UPDATE sesiones_caja SET estado = 'cerrada', cierre = ?, usuario_cierre = ?, esperado = ?, contado = ?,"
        " diferencia = ?, motivo = ?, autorizado_por = ? WHERE id = ? AND estado = 'abierta'"));
    q.addBindValue(QDateTime::currentDateTime());
    q.addBindValue(userName());
    q.addBindValue(expected);
    q.addBindValue(counted);
    q.addBindValue(counted - expected);
    q.addBindValue(reason);
    q.addBindValue(authorizedBy);
    q.addBindValue(session.id);
    if (!exec(q) || q.numRowsAffected() != 1 || !writeCount(session.id, CountClosing, count)) {
        if (m_lastError.isEmpty())
            m_lastError = tr("La sesión ya estaba cerrada.");
        rollback();
        return false;
    }
    if (!commit())
        return false;
    audit(QStringLiteral("caja.cerrar"), QString::number(session.id),
          tr("Esperado Gs. %1, contado Gs. %2").arg(AguaraMoney::format(expected), AguaraMoney::format(counted))
              + (reason.isEmpty() ? QString() : QStringLiteral(". ") + reason));
    m_knownSession = std::nullopt;
    return true;
}

CloseResult SqlCashService::closeSession(const CashCount &count)
{
    m_lastError.clear();
    if (salesArePending())
        return CloseResult::Failed;
    const std::optional<CashSession> session = currentSession();
    if (!session) {
        m_lastError = tr("La caja no está abierta.");
        return CloseResult::Failed;
    }
    const Money expected = expectedCash(session->id, session->openingFloat);
    const Money counted = AguaraCash::countTotal(count);
    if (counted != expected) {
        audit(QStringLiteral("caja.arqueo_no_coincide"), QString::number(session->id),
              tr("Esperado Gs. %1, contado Gs. %2").arg(AguaraMoney::format(expected), AguaraMoney::format(counted)));
        return CloseResult::Mismatch;
    }
    return finish(*session, count, expected, QString(), QString()) ? CloseResult::Closed : CloseResult::Failed;
}

bool SqlCashService::closeWithDifference(const CashCount &count, const QString &reason, const QString &authorizedBy)
{
    m_lastError.clear();
    if (reason.trimmed().isEmpty()) {
        m_lastError = tr("Indique el motivo de la diferencia.");
        return false;
    }
    if (salesArePending())
        return false;
    const std::optional<CashSession> session = currentSession();
    if (!session) {
        m_lastError = tr("La caja no está abierta.");
        return false;
    }
    return finish(*session, count, expectedCash(session->id, session->openingFloat), reason.trimmed(), authorizedBy);
}

QList<CashSession> SqlCashService::sessions(int limit) const
{
    return readSessions(QStringLiteral("ORDER BY s.id DESC LIMIT %1").arg(qMax(limit, 1)), {});
}

std::optional<CashSessionReport> SqlCashService::sessionReport(qint64 sessionId) const
{
    const QList<CashSession> found = readSessions(QStringLiteral("WHERE s.id = ?"), { sessionId });
    if (found.isEmpty()) {
        m_lastError = tr("La sesión no existe.");
        return std::nullopt;
    }

    CashSessionReport r;
    r.session = found.first();
    r.expectedCash = r.session.open ? expectedCash(sessionId, r.session.openingFloat) : r.session.expected;
    r.openingCount = readCount(sessionId, CountOpening);
    r.closingCount = readCount(sessionId, CountClosing);

    QSqlQuery totals(db());
    totals.prepare(QStringLiteral(
        "SELECT estado, COUNT(*), CAST(SUM(total) AS BIGINT), CAST(SUM(descuento) AS BIGINT),"
        " CAST(SUM(redondeo) AS BIGINT), CAST(SUM(gravado10) AS BIGINT), CAST(SUM(iva10) AS BIGINT),"
        " CAST(SUM(gravado5) AS BIGINT), CAST(SUM(iva5) AS BIGINT), CAST(SUM(exento) AS BIGINT)"
        " FROM ventas WHERE sesion_id = ? GROUP BY estado"));
    totals.addBindValue(sessionId);
    if (!exec(totals))
        return std::nullopt;
    while (totals.next()) {
        if (totals.value(0).toString() == QLatin1String(SaleState::Voided)) {
            r.voidedCount = totals.value(1).toInt();
            r.voidedTotal = totals.value(2).toLongLong();
            continue;
        }
        r.salesCount = totals.value(1).toInt();
        r.salesTotal = totals.value(2).toLongLong();
        r.discounts = totals.value(3).toLongLong();
        r.rounding = totals.value(4).toLongLong();
        r.taxed10 = totals.value(5).toLongLong();
        r.vat10 = totals.value(6).toLongLong();
        r.taxed5 = totals.value(7).toLongLong();
        r.vat5 = totals.value(8).toLongLong();
        r.exempt = totals.value(9).toLongLong();
    }

    QSqlQuery payments(db());
    payments.prepare(QStringLiteral(
        "SELECT p.medio, CAST(SUM(p.monto - p.vuelto) AS BIGINT) FROM ventas_pagos p"
        " JOIN ventas v ON v.id = p.venta_id WHERE v.sesion_id = ? AND v.estado = ?"
        " GROUP BY p.medio ORDER BY p.medio"));
    payments.addBindValue(sessionId);
    payments.addBindValue(QString::fromLatin1(SaleState::Paid));
    if (!exec(payments))
        return std::nullopt;
    while (payments.next()) {
        PaymentTotal p;
        p.method = payments.value(0).toString();
        p.amount = payments.value(1).toLongLong();
        r.payments << p;
    }

    QSqlQuery vouchers(db());
    vouchers.prepare(QStringLiteral(
        "SELECT v.caja_id, v.numero, p.medio, p.referencia, p.monto FROM ventas_pagos p"
        " JOIN ventas v ON v.id = p.venta_id WHERE v.sesion_id = ? AND v.estado = ? AND p.medio <> ?"
        " ORDER BY p.medio, p.id"));
    vouchers.addBindValue(sessionId);
    vouchers.addBindValue(QString::fromLatin1(SaleState::Paid));
    vouchers.addBindValue(QString::fromLatin1(PaymentMethod::Cash));
    if (!exec(vouchers))
        return std::nullopt;
    while (vouchers.next()) {
        PaymentVoucher v;
        v.saleNumber = AguaraSale::formatNumber(vouchers.value(0).toLongLong(), vouchers.value(1).toLongLong());
        v.method = vouchers.value(2).toString();
        v.reference = vouchers.value(3).toString();
        v.amount = vouchers.value(4).toLongLong();
        r.vouchers << v;
    }

    r.movements = movements(sessionId);
    for (const CashMovement &m : r.movements) {
        if (m.type == QLatin1String(CashMovementType::In))
            r.cashIn += m.amount;
        else
            r.cashOut += m.amount;
    }
    return r;
}
