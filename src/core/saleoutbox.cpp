#include "saleoutbox.h"
#include "database.h"
#include "logging.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

SaleOutbox::SaleOutbox(const QString &connectionName)
    : m_connectionName(QStringLiteral("outbox-") + connectionName)
{
}

SaleOutbox::~SaleOutbox()
{
    if (!QSqlDatabase::contains(m_connectionName))
        return;
    QSqlDatabase::database(m_connectionName, false).close();
    QSqlDatabase::removeDatabase(m_connectionName);
}

QString SaleOutbox::path() const
{
    if (!m_path.isEmpty())
        return m_path;
    return QFileInfo(DatabaseSettings::settingsPath()).dir().filePath(
        QStringLiteral("ventas-pendientes-%1.db").arg(m_connectionName.mid(7)));
}

bool SaleOutbox::open()
{
    if (m_open)
        return true;
    const QString file = path();
    QDir().mkpath(QFileInfo(file).absolutePath());

    QSqlDatabase db = QSqlDatabase::contains(m_connectionName)
        ? QSqlDatabase::database(m_connectionName, false)
        : QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    db.setDatabaseName(file);
    if (!db.open()) {
        qCCritical(lcDatabase).noquote() << "Sales queue" << file << ":" << db.lastError().text();
        m_lastError = QObject::tr("No se pudo abrir la cola de ventas de este equipo.");
        return false;
    }
    QSqlQuery q(db);
    // FULL: a commit returns once the sale is on the disk, not in Windows' cache
    if (!q.exec(QStringLiteral("PRAGMA synchronous = FULL"))
        || !q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS pendientes ("
            " id          INTEGER PRIMARY KEY AUTOINCREMENT,"       // never reused
            " uuid        TEXT NOT NULL UNIQUE,"
            " fecha       TEXT NOT NULL,"
            " usuario     TEXT,"
            " caja_id     INTEGER NOT NULL,"
            " sesion_id   INTEGER NOT NULL,"
            " numero      INTEGER NOT NULL DEFAULT 0,"
            " impresiones INTEGER NOT NULL DEFAULT 0,"
            " venta       TEXT NOT NULL,"
            " pagos       TEXT NOT NULL,"
            " error       TEXT)"))) {
        qCCritical(lcDatabase).noquote() << "Sales queue" << file << ":" << q.lastError().text();
        m_lastError = QObject::tr("No se pudo preparar la cola de ventas de este equipo.");
        return false;
    }
    m_open = true;
    return true;
}

bool SaleOutbox::run(const QString &sql, const QVariantList &values) const
{
    QSqlQuery q(QSqlDatabase::database(m_connectionName, false));
    q.prepare(sql);
    for (const QVariant &value : values)
        q.addBindValue(value);
    if (q.exec())
        return true;
    qCCritical(lcDatabase).noquote() << "Sales queue:" << q.lastError().text();
    m_lastError = QObject::tr("No se pudo escribir en la cola de ventas de este equipo.");
    return false;
}

qint64 SaleOutbox::add(const OutboxEntry &e)
{
    QSqlQuery q(QSqlDatabase::database(m_connectionName, false));
    q.prepare(QStringLiteral("INSERT INTO pendientes (uuid, fecha, usuario, caja_id, sesion_id, numero, impresiones,"
                             " venta, pagos) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(e.uuid);
    q.addBindValue(e.when.toString(Qt::ISODateWithMs));
    q.addBindValue(e.user);
    q.addBindValue(e.registerId);
    q.addBindValue(e.sessionId);
    q.addBindValue(e.number);
    q.addBindValue(e.printCount);
    q.addBindValue(QString::fromUtf8(SaleJson::fromDraft(e.draft)));
    q.addBindValue(QString::fromUtf8(SaleJson::fromPayments(e.payments)));
    if (!q.exec()) {
        qCCritical(lcDatabase).noquote() << "Sales queue:" << q.lastError().text();
        m_lastError = QObject::tr("No se pudo guardar la venta en este equipo. No se cobró.");
        return 0;
    }
    return q.lastInsertId().toLongLong();
}

bool SaleOutbox::setNumber(qint64 id, qint64 number)
{
    return run(QStringLiteral("UPDATE pendientes SET numero = ? WHERE id = ?"), { number, id });
}

bool SaleOutbox::addPrint(qint64 id)
{
    return run(QStringLiteral("UPDATE pendientes SET impresiones = impresiones + 1 WHERE id = ?"), { id });
}

bool SaleOutbox::setError(qint64 id, const QString &error)
{
    return run(QStringLiteral("UPDATE pendientes SET error = ? WHERE id = ?"), { error, id });
}

bool SaleOutbox::remove(qint64 id)
{
    return run(QStringLiteral("DELETE FROM pendientes WHERE id = ?"), { id });
}

QList<OutboxEntry> SaleOutbox::read(const QString &where, const QVariantList &values) const
{
    QList<OutboxEntry> result;
    QSqlQuery q(QSqlDatabase::database(m_connectionName, false));
    q.prepare(QStringLiteral("SELECT id, uuid, fecha, usuario, caja_id, sesion_id, numero, impresiones, venta, pagos, error"
                             " FROM pendientes ") + where);
    for (const QVariant &value : values)
        q.addBindValue(value);
    if (!q.exec()) {
        qCCritical(lcDatabase).noquote() << "Sales queue:" << q.lastError().text();
        return result;
    }
    while (q.next()) {
        OutboxEntry e;
        e.id = q.value(0).toLongLong();
        e.uuid = q.value(1).toString();
        e.when = QDateTime::fromString(q.value(2).toString(), Qt::ISODateWithMs);
        e.user = q.value(3).toString();
        e.registerId = q.value(4).toLongLong();
        e.sessionId = q.value(5).toLongLong();
        e.number = q.value(6).toLongLong();
        e.printCount = q.value(7).toInt();
        e.draft = SaleJson::toDraft(q.value(8).toString().toUtf8());
        e.payments = SaleJson::toPayments(q.value(9).toString().toUtf8());
        e.error = q.value(10).toString();
        result << e;
    }
    return result;
}

QList<OutboxEntry> SaleOutbox::entries() const
{
    return read(QStringLiteral("ORDER BY id"), {});
}

std::optional<OutboxEntry> SaleOutbox::entry(qint64 id) const
{
    const QList<OutboxEntry> found = read(QStringLiteral("WHERE id = ?"), { id });
    if (found.isEmpty())
        return std::nullopt;
    return found.first();
}

int SaleOutbox::count() const
{
    QSqlQuery q(QSqlDatabase::database(m_connectionName, false));
    return q.exec(QStringLiteral("SELECT COUNT(*) FROM pendientes")) && q.next() ? q.value(0).toInt() : 0;
}

qint64 SaleOutbox::lastNumber(qint64 registerId) const
{
    QSqlQuery q(QSqlDatabase::database(m_connectionName, false));
    q.prepare(QStringLiteral("SELECT COALESCE(MAX(numero), 0) FROM pendientes WHERE caja_id = ?"));
    q.addBindValue(registerId);
    return q.exec() && q.next() ? q.value(0).toLongLong() : 0;
}

// --- JSON --------------------------------------------------------------------

QByteArray SaleJson::fromDraft(const SaleDraft &draft)
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

SaleDraft SaleJson::toDraft(const QByteArray &json)
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

QByteArray SaleJson::fromPayments(const QList<SalePayment> &payments)
{
    QJsonArray list;
    for (const SalePayment &p : payments) {
        QJsonObject o;
        o[QStringLiteral("method")] = p.method;
        o[QStringLiteral("amount")] = QString::number(p.amount);
        o[QStringLiteral("reference")] = p.reference;
        list.append(o);
    }
    return QJsonDocument(list).toJson(QJsonDocument::Compact);
}

QList<SalePayment> SaleJson::toPayments(const QByteArray &json)
{
    QList<SalePayment> result;
    for (const QJsonValue &value : QJsonDocument::fromJson(json).array()) {
        const QJsonObject o = value.toObject();
        SalePayment p;
        p.method = o.value(QStringLiteral("method")).toString();
        p.amount = o.value(QStringLiteral("amount")).toString().toLongLong();
        p.reference = o.value(QStringLiteral("reference")).toString();
        result << p;
    }
    return result;
}
