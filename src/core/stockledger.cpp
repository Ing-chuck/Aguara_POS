#include "stockledger.h"
#include "connectionmonitor.h"

#include <userservice.h>

#include <inventoryservice.h>

#include <QList>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSysInfo>
#include <QVariant>

#include <algorithm>
#include <utility>

bool StockLedger::run(QSqlQuery &query)
{
    if (m_monitor->exec(query, true))
        return true;
    *m_error = m_monitor->lastError();
    return false;
}

bool StockLedger::move(const QString &code, const char *type, Quantity delta, Money unitCost,
                       const QString &reason, const QString &document, const QDate &expires)
{
    const QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);

    // The UPDATE takes the row lock: two tills selling the same article get
    // consecutive balances.
    QSqlQuery update(db);
    update.prepare(QStringLiteral("UPDATE productos SET stock = stock + ? WHERE codigo = ?"));
    update.addBindValue(delta);
    update.addBindValue(code);
    if (!m_monitor->exec(update, true)) {
        *m_error = m_monitor->lastError();
        return false;
    }

    QSqlQuery read(db);
    read.prepare(QStringLiteral("SELECT stock FROM productos WHERE codigo = ?"));
    read.addBindValue(code);
    if (!m_monitor->exec(read, true) || !read.next()) {
        *m_error = m_monitor->lastError();
        return false;
    }
    const Quantity balance = read.value(0).toLongLong();
    read.finish();

    const auto user = m_users ? m_users->currentUser() : std::nullopt;
    QSqlQuery insert(db);
    insert.prepare(QStringLiteral(
        "INSERT INTO stock_movimientos (producto_codigo, tipo, cantidad, saldo, costo_unitario,"
        " motivo, documento, usuario, puesto) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    insert.addBindValue(code);
    insert.addBindValue(QString::fromLatin1(type));
    insert.addBindValue(delta);
    insert.addBindValue(balance);
    insert.addBindValue(unitCost);
    insert.addBindValue(reason);
    insert.addBindValue(document);
    insert.addBindValue(user ? user->username : QString());
    insert.addBindValue(QSysInfo::machineHostName());
    if (!run(insert))
        return false;

    const bool sale = qstrcmp(type, StockMovementType::Sale) == 0;
    if (delta < 0) {
        if (!takeFromLots(code, -delta, sale ? Taken::Sold : Taken::WrittenOff, !sale))
            return false;
    } else if (expires.isValid()) {
        if (!addToLot(code, expires, delta))
            return false;
    } else if (qstrcmp(type, StockMovementType::Return) == 0 && !refillLots(code, delta)) {
        return false;
    }

    // Lots can't hold more than the stock. What's left over: a sale that had only
    // expired stock to take, or stock below zero that ate part of an entry.
    const std::optional<Quantity> dated = inLots(code);
    if (!dated)
        return false;
    const Quantity excess = *dated - qMax(balance, Quantity(0));
    return excess <= 0 || takeFromLots(code, excess, sale ? Taken::Sold : Taken::WrittenOff);
}

std::optional<Quantity> StockLedger::inLots(const QString &code)
{
    QSqlQuery q(QSqlDatabase::database(m_connectionName, false));
    q.prepare(QStringLiteral("SELECT COALESCE(SUM(cantidad), 0) FROM lotes WHERE producto_codigo = ?"));
    q.addBindValue(code);
    if (!run(q) || !q.next())
        return std::nullopt;
    return q.value(0).toLongLong();
}

std::optional<Quantity> StockLedger::undated(const QString &code)
{
    const std::optional<Quantity> dated = inLots(code);
    if (!dated)
        return std::nullopt;
    QSqlQuery q(QSqlDatabase::database(m_connectionName, false));
    q.prepare(QStringLiteral("SELECT stock FROM productos WHERE codigo = ?"));
    q.addBindValue(code);
    if (!run(q) || !q.next())
        return std::nullopt;
    return qMax(q.value(0).toLongLong() - *dated, Quantity(0));
}

bool StockLedger::addToLot(const QString &code, const QDate &expires, Quantity quantity)
{
    const QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
    const QString date = expires.toString(Qt::ISODate);
    QSqlQuery find(db);
    find.prepare(QStringLiteral("SELECT id FROM lotes WHERE producto_codigo = ? AND vencimiento = ?"));
    find.addBindValue(code);
    find.addBindValue(date);
    if (!run(find))
        return false;
    const qint64 lot = find.next() ? find.value(0).toLongLong() : 0;
    find.finish();

    QSqlQuery write(db);
    if (lot) {
        write.prepare(QStringLiteral("UPDATE lotes SET cantidad = cantidad + ?, inicial = inicial + ? WHERE id = ?"));
        write.addBindValue(quantity);
        write.addBindValue(quantity);
        write.addBindValue(lot);
    } else {
        write.prepare(QStringLiteral("INSERT INTO lotes (producto_codigo, vencimiento, cantidad, inicial) VALUES (?, ?, ?, ?)"));
        write.addBindValue(code);
        write.addBindValue(date);
        write.addBindValue(quantity);
        write.addBindValue(quantity);
    }
    return run(write);
}

bool StockLedger::takeFromLots(const QString &code, Quantity quantity, Taken how, bool includeExpired)
{
    const QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT id, cantidad FROM lotes WHERE producto_codigo = ? AND cantidad > 0%1"
                             " ORDER BY vencimiento, id")
                  .arg(includeExpired ? QString() : QStringLiteral(" AND vencimiento >= ?")));
    q.addBindValue(code);
    if (!includeExpired)
        q.addBindValue(QDate::currentDate().toString(Qt::ISODate));     // the last day is still good
    if (!run(q))
        return false;
    QList<std::pair<qint64, Quantity>> lots;
    while (q.next())
        lots.append({ q.value(0).toLongLong(), q.value(1).toLongLong() });
    q.finish();

    for (const auto &lot : qAsConst(lots)) {
        if (quantity <= 0)
            break;
        const Quantity taken = qMin(quantity, lot.second);
        QSqlQuery write(db);
        // what was written off won't come back: a return must not refill it
        write.prepare(how == Taken::Sold
            ? QStringLiteral("UPDATE lotes SET cantidad = cantidad - ? WHERE id = ?")
            : QStringLiteral("UPDATE lotes SET cantidad = cantidad - ?, inicial = inicial - ? WHERE id = ?"));
        write.addBindValue(taken);
        if (how == Taken::WrittenOff)
            write.addBindValue(taken);
        write.addBindValue(lot.first);
        if (!run(write))
            return false;
        quantity -= taken;
    }
    return true;
}

bool StockLedger::refillLots(const QString &code, Quantity quantity)
{
    const QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT id, inicial - cantidad FROM lotes WHERE producto_codigo = ? AND cantidad < inicial"
                             " ORDER BY vencimiento, id"));
    q.addBindValue(code);
    if (!run(q))
        return false;
    QList<std::pair<qint64, Quantity>> lots;
    while (q.next())
        lots.append({ q.value(0).toLongLong(), q.value(1).toLongLong() });
    q.finish();

    for (const auto &lot : qAsConst(lots)) {
        if (quantity <= 0)
            break;
        const Quantity back = qMin(quantity, lot.second);
        QSqlQuery write(db);
        write.prepare(QStringLiteral("UPDATE lotes SET cantidad = cantidad + ? WHERE id = ?"));
        write.addBindValue(back);
        write.addBindValue(lot.first);
        if (!run(write))
            return false;
        quantity -= back;
    }
    return true;
}

bool StockLedger::recordPrice(const QString &code, Money oldPrice, Money newPrice, Money oldCost, Money newCost,
                              const QString &reason)
{
    if (oldPrice == newPrice && oldCost == newCost)
        return true;
    const auto user = m_users ? m_users->currentUser() : std::nullopt;
    QSqlQuery insert(QSqlDatabase::database(m_connectionName, false));
    insert.prepare(QStringLiteral(
        "INSERT INTO precios_historial (producto_codigo, precio_anterior, precio_nuevo, costo_anterior,"
        " costo_nuevo, motivo, usuario) VALUES (?, ?, ?, ?, ?, ?, ?)"));
    insert.addBindValue(code);
    insert.addBindValue(oldPrice);
    insert.addBindValue(newPrice);
    insert.addBindValue(oldCost);
    insert.addBindValue(newCost);
    insert.addBindValue(reason);
    insert.addBindValue(user ? user->username : QString());
    if (!m_monitor->exec(insert, true)) {
        *m_error = m_monitor->lastError();
        return false;
    }
    return true;
}
