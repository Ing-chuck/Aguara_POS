#include "sqlcustomerservice.h"
#include "connectionmonitor.h"

#include <userservice.h>

#include <QSqlQuery>
#include <QVariant>

namespace {

const char *const kColumns = "id, nombre, tipo_documento, documento, direccion, telefono, correo, activo";

Customer read(const QSqlQuery &q)
{
    Customer c;
    c.id = q.value(0).toLongLong();
    c.name = q.value(1).toString();
    c.documentType = q.value(2).toString();
    c.document = q.value(3).toString();
    c.address = q.value(4).toString();
    c.phone = q.value(5).toString();
    c.email = q.value(6).toString();
    c.active = q.value(7).toInt() != 0;
    return c;
}

} // namespace

SqlCustomerService::SqlCustomerService(ConnectionMonitor *monitor, QObject *parent)
    : QObject(parent), m_monitor(monitor), m_connectionName(monitor->connectionName())
{
}

QSqlDatabase SqlCustomerService::db() const
{
    return QSqlDatabase::database(m_connectionName, false);
}

bool SqlCustomerService::exec(QSqlQuery &q) const
{
    if (m_monitor->exec(q))
        return true;
    m_lastError = m_monitor->lastError();
    return false;
}

void SqlCustomerService::audit(const QString &action, qint64 id, const QString &after)
{
    if (m_users)
        m_users->audit(action, QStringLiteral("cliente"), QString::number(id), QString(), after);
}

QList<Customer> SqlCustomerService::customers(const QString &filter, bool inactive) const
{
    QList<Customer> result;
    const QString text = filter.trimmed().toLower();
    QString sql = QStringLiteral("SELECT %1 FROM clientes WHERE 1 = 1").arg(QLatin1String(kColumns));
    if (!inactive)
        sql += QStringLiteral(" AND activo = 1");
    if (!text.isEmpty())
        sql += QStringLiteral(" AND (lower(nombre) LIKE ? OR lower(documento) LIKE ?)");
    QSqlQuery q(db());
    q.prepare(sql + QStringLiteral(" ORDER BY lower(nombre) LIMIT 500"));
    if (!text.isEmpty()) {
        const QString like = QLatin1Char('%') + text + QLatin1Char('%');
        q.addBindValue(like);
        q.addBindValue(like);
    }
    if (!exec(q))
        return result;
    while (q.next())
        result << read(q);
    return result;
}

std::optional<Customer> SqlCustomerService::customer(qint64 id) const
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT %1 FROM clientes WHERE id = ?").arg(QLatin1String(kColumns)));
    q.addBindValue(id);
    if (!exec(q))
        return std::nullopt;
    if (!q.next()) {
        m_lastError = tr("El cliente no existe.");
        return std::nullopt;
    }
    return read(q);
}

qint64 SqlCustomerService::saveCustomer(const Customer &customer)
{
    Customer c = customer;
    c.name = c.name.simplified();
    c.document = c.document.trimmed().toUpper();
    if (c.name.isEmpty()) {
        m_lastError = tr("Escriba el nombre del cliente.");
        return 0;
    }
    if (c.documentType == QLatin1String(DocumentType::Ruc) && !AguaraCustomer::validRuc(c.document)) {
        m_lastError = tr("El RUC no es válido: revise el número y su dígito verificador (80009735-1).");
        return 0;
    }
    if (c.documentType == QLatin1String(DocumentType::Ci) && !AguaraCustomer::validCi(c.document)) {
        m_lastError = tr("La cédula lleva solo números.");
        return 0;
    }
    if (c.documentType.isEmpty())
        c.document.clear();

    if (!c.document.isEmpty()) {
        QSqlQuery same(db());
        same.prepare(QStringLiteral("SELECT nombre FROM clientes WHERE documento = ? AND id <> ?"));
        same.addBindValue(c.document);
        same.addBindValue(c.id);
        if (!exec(same))
            return 0;
        if (same.next()) {
            m_lastError = tr("Ese documento ya es de %1.").arg(same.value(0).toString());
            return 0;
        }
    }

    QSqlQuery q(db());
    q.prepare(c.id == 0
        ? QStringLiteral("INSERT INTO clientes (nombre, tipo_documento, documento, direccion, telefono, correo)"
                         " VALUES (?, ?, ?, ?, ?, ?)")
        : QStringLiteral("UPDATE clientes SET nombre = ?, tipo_documento = ?, documento = ?, direccion = ?,"
                         " telefono = ?, correo = ? WHERE id = ?"));
    q.addBindValue(c.name);
    q.addBindValue(c.documentType.isNull() ? QStringLiteral("") : c.documentType);
    q.addBindValue(c.document.isEmpty() ? QVariant(QVariant::String) : QVariant(c.document));   // many without one
    q.addBindValue(c.address.trimmed());
    q.addBindValue(c.phone.trimmed());
    q.addBindValue(c.email.trimmed());
    if (c.id != 0)
        q.addBindValue(c.id);
    if (!exec(q))
        return 0;
    const qint64 id = c.id != 0 ? c.id : q.lastInsertId().toLongLong();
    audit(c.id != 0 ? QStringLiteral("cliente.modificar") : QStringLiteral("cliente.crear"), id, c.name);
    return id;
}

bool SqlCustomerService::setCustomerActive(qint64 id, bool active)
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("UPDATE clientes SET activo = ? WHERE id = ?"));
    q.addBindValue(active ? 1 : 0);
    q.addBindValue(id);
    if (!exec(q))
        return false;
    audit(active ? QStringLiteral("cliente.reactivar") : QStringLiteral("cliente.baja"), id, QString());
    return true;
}
