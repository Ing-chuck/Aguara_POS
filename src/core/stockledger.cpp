#include "stockledger.h"
#include "connectionmonitor.h"

#include <userservice.h>

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSysInfo>
#include <QVariant>

bool StockLedger::move(const QString &code, const char *type, Quantity delta, Money unitCost,
                       const QString &reason, const QString &document)
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
    if (!m_monitor->exec(insert, true)) {
        *m_error = m_monitor->lastError();
        return false;
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
