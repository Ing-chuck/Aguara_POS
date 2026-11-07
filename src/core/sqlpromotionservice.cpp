#include "sqlpromotionservice.h"
#include "connectionmonitor.h"
#include "errors.h"

#include <userservice.h>

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <algorithm>

namespace {

QString kindCode(PromotionKind kind)
{
    switch (kind) {
    case PromotionKind::Percent:    return QStringLiteral("porcentaje");
    case PromotionKind::AmountOff:  return QStringLiteral("monto");
    case PromotionKind::FixedPrice: return QStringLiteral("precio");
    case PromotionKind::BuyPay:     return QStringLiteral("lleva_paga");
    }
    return QString();
}

PromotionKind kindOf(const QString &code)
{
    if (code == QLatin1String("monto"))
        return PromotionKind::AmountOff;
    if (code == QLatin1String("precio"))
        return PromotionKind::FixedPrice;
    return code == QLatin1String("lleva_paga") ? PromotionKind::BuyPay : PromotionKind::Percent;
}

/// The category and the ones above it. The steps are counted: a loop in the
/// data must not hang a till.
QList<qint64> withParents(qint64 category, const QHash<qint64, qint64> &parents)
{
    QList<qint64> line;
    for (qint64 id = category; id != 0 && line.size() < 64; id = parents.value(id))
        line << id;
    return line;
}

} // namespace

SqlPromotionService::SqlPromotionService(ConnectionMonitor *monitor, QObject *parent)
    : QObject(parent), m_monitor(monitor), m_connectionName(monitor->connectionName())
{
}

QSqlDatabase SqlPromotionService::db() const
{
    return QSqlDatabase::database(m_connectionName, false);
}

bool SqlPromotionService::exec(QSqlQuery &q) const
{
    if (m_monitor->exec(q, m_inTransaction))
        return true;
    m_lastError = m_monitor->lastError();
    return false;
}

std::optional<QList<Promotion>> SqlPromotionService::read(bool inactive) const
{
    QSqlQuery q(db());
    q.setForwardOnly(true);
    q.prepare(QStringLiteral(
        "SELECT id, nombre, tipo, valor, lleva, paga, minimo, desde, hasta, dias, todos, rubro_y_marca, invertir, activo"
        " FROM promociones%1 ORDER BY hasta DESC, id").arg(inactive ? QString() : QStringLiteral(" WHERE activo = 1")));
    if (!exec(q))
        return std::nullopt;
    QList<Promotion> result;
    QHash<qint64, int> rows;
    while (q.next()) {
        Promotion p;
        p.id = q.value(0).toLongLong();
        p.name = q.value(1).toString();
        p.kind = kindOf(q.value(2).toString());
        p.value = q.value(3).toLongLong();
        p.buy = q.value(4).toInt();
        p.pay = q.value(5).toInt();
        p.minimum = q.value(6).toLongLong();
        p.from = q.value(7).toDate();
        p.to = q.value(8).toDate();
        p.weekdays = q.value(9).toInt();
        p.everything = q.value(10).toInt() != 0;
        p.categoryAndBrand = q.value(11).toInt() != 0;
        p.inverted = q.value(12).toInt() != 0;
        p.active = q.value(13).toInt() != 0;
        rows[p.id] = result.size();
        result << p;
    }
    if (result.isEmpty())
        return result;

    QSqlQuery rules(db());
    rules.setForwardOnly(true);
    rules.prepare(QStringLiteral(
        "SELECT g.promocion_id, g.producto_codigo, g.rubro_id, g.marca_id, g.excluir,"
        "       COALESCE(p.descripcion, r.rubro, m.nombre)"
        " FROM promocion_reglas g"
        " LEFT JOIN productos p ON p.codigo = g.producto_codigo"
        " LEFT JOIN rubros r ON r.id = g.rubro_id"
        " LEFT JOIN marcas m ON m.id = g.marca_id"
        " ORDER BY g.id"));
    if (!exec(rules))
        return std::nullopt;
    while (rules.next()) {
        const auto row = rows.constFind(rules.value(0).toLongLong());
        if (row == rows.constEnd())
            continue;                   // of one not asked for
        PromotionRule rule;
        if (!rules.value(1).isNull()) {
            rule.target = PromotionRule::Article;
            rule.code = rules.value(1).toString();
        } else if (!rules.value(2).isNull()) {
            rule.target = PromotionRule::Category;
            rule.id = rules.value(2).toLongLong();
        } else {
            rule.target = PromotionRule::Brand;
            rule.id = rules.value(3).toLongLong();
        }
        rule.excluded = rules.value(4).toInt() != 0;
        rule.name = rules.value(5).toString();
        result[row.value()].rules << rule;
    }
    return result;
}

QList<Promotion> SqlPromotionService::promotions(bool inactive) const
{
    return read(inactive).value_or(QList<Promotion>());
}

bool SqlPromotionService::valid(const Promotion &p) const
{
    bool includes = false;
    for (const PromotionRule &rule : p.rules)
        includes = includes || !rule.excluded;

    if (p.name.simplified().isEmpty())
        m_lastError = tr("Escriba el nombre de la promoción.");
    else if (p.kind == PromotionKind::Percent && (p.value < 1 || p.value > 100))
        m_lastError = tr("El porcentaje va de 1 a 100.");
    else if ((p.kind == PromotionKind::AmountOff || p.kind == PromotionKind::FixedPrice) && p.value <= 0)
        m_lastError = tr("Escriba el importe de la promoción.");
    else if (p.kind == PromotionKind::BuyPay && (p.pay < 1 || p.buy <= p.pay))
        m_lastError = tr("Debe llevar más de lo que paga, y pagar al menos 1.");
    else if (p.minimum < 0)
        m_lastError = tr("La cantidad mínima no es válida.");
    else if (!p.from.isValid() || !p.to.isValid() || p.to < p.from)
        m_lastError = tr("La fecha de fin no puede ser anterior a la de inicio.");
    else if ((p.weekdays & AguaraPromotion::AllWeekdays) == 0)
        m_lastError = tr("Elija al menos un día de la semana.");
    else if (p.everything && p.inverted)
        m_lastError = tr("Invertir todos los artículos no deja ninguno.");
    else if (!p.everything && !includes)
        m_lastError = tr("Elija a qué artículos se aplica.");
    else
        return true;
    return false;
}

bool SqlPromotionService::writeRules(qint64 id, const QList<PromotionRule> &rules)
{
    QSqlQuery clear(db());
    clear.prepare(QStringLiteral("DELETE FROM promocion_reglas WHERE promocion_id = ?"));
    clear.addBindValue(id);
    if (!exec(clear))
        return false;
    for (const PromotionRule &rule : rules) {
        QSqlQuery q(db());
        q.prepare(QStringLiteral(
            "INSERT INTO promocion_reglas (promocion_id, producto_codigo, rubro_id, marca_id, excluir)"
            " VALUES (?, ?, ?, ?, ?)"));
        q.addBindValue(id);
        q.addBindValue(rule.target == PromotionRule::Article ? QVariant(rule.code) : QVariant(QVariant::String));
        q.addBindValue(rule.target == PromotionRule::Category ? QVariant(rule.id) : QVariant(QVariant::LongLong));
        q.addBindValue(rule.target == PromotionRule::Brand ? QVariant(rule.id) : QVariant(QVariant::LongLong));
        q.addBindValue(rule.excluded ? 1 : 0);
        if (!exec(q))
            return false;
    }
    return true;
}

qint64 SqlPromotionService::savePromotion(const Promotion &promotion)
{
    if (!valid(promotion))
        return 0;
    Promotion p = promotion;
    p.name = p.name.simplified();

    if (m_monitor->connectionBroken())
        m_monitor->reconnect();
    if (!db().transaction()) {
        m_lastError = AguaraErrors::logAndExplain(db().lastError(), QStringLiteral("BEGIN"));
        return 0;
    }
    m_inTransaction = true;
    QSqlQuery q(db());
    q.prepare(p.id == 0
        ? QStringLiteral("INSERT INTO promociones (nombre, tipo, valor, lleva, paga, minimo, desde, hasta, dias, todos,"
                         " rubro_y_marca, invertir, activo) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)")
        : QStringLiteral("UPDATE promociones SET nombre = ?, tipo = ?, valor = ?, lleva = ?, paga = ?, minimo = ?,"
                         " desde = ?, hasta = ?, dias = ?, todos = ?, rubro_y_marca = ?, invertir = ?, activo = ?"
                         " WHERE id = ?"));
    q.addBindValue(p.name);
    q.addBindValue(kindCode(p.kind));
    q.addBindValue(p.kind == PromotionKind::BuyPay ? Money(0) : p.value);
    q.addBindValue(p.kind == PromotionKind::BuyPay ? p.buy : 0);
    q.addBindValue(p.kind == PromotionKind::BuyPay ? p.pay : 0);
    q.addBindValue(p.minimum);
    q.addBindValue(p.from.toString(Qt::ISODate));
    q.addBindValue(p.to.toString(Qt::ISODate));
    q.addBindValue(p.weekdays);
    q.addBindValue(p.everything ? 1 : 0);
    q.addBindValue(p.categoryAndBrand ? 1 : 0);
    q.addBindValue(p.inverted ? 1 : 0);
    q.addBindValue(p.active ? 1 : 0);
    if (p.id != 0)
        q.addBindValue(p.id);
    const bool wrote = exec(q);
    const qint64 id = p.id != 0 ? p.id : q.lastInsertId().toLongLong();
    if (!wrote || !writeRules(id, p.rules)) {
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
        m_users->audit(p.id != 0 ? QStringLiteral("promocion.modificar") : QStringLiteral("promocion.crear"),
                       QStringLiteral("promocion"), QString::number(id), QString(),
                       QStringLiteral("%1: %2, %3 a %4").arg(p.name, AguaraPromotion::describe(p),
                           p.from.toString(QStringLiteral("dd/MM/yyyy")), p.to.toString(QStringLiteral("dd/MM/yyyy"))));
    return id;
}

bool SqlPromotionService::setPromotionActive(qint64 id, bool active)
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("UPDATE promociones SET activo = ? WHERE id = ?"));
    q.addBindValue(active ? 1 : 0);
    q.addBindValue(id);
    if (!exec(q))
        return false;
    if (m_users)
        m_users->audit(active ? QStringLiteral("promocion.reactivar") : QStringLiteral("promocion.baja"),
                       QStringLiteral("promocion"), QString::number(id));
    return true;
}

std::optional<QHash<qint64, qint64>> SqlPromotionService::categoryParents() const
{
    QSqlQuery q(db());
    q.setForwardOnly(true);
    q.prepare(QStringLiteral("SELECT id, parent_id FROM rubros"));
    if (!exec(q))
        return std::nullopt;
    QHash<qint64, qint64> parents;
    while (q.next())
        parents[q.value(0).toLongLong()] = q.value(1).toLongLong();
    return parents;
}

std::optional<QList<Promotion>> SqlPromotionService::offersFor(const QString &code, const QDate &day) const
{
    const std::optional<QList<Promotion>> all = read(false);
    if (!all)
        return std::nullopt;
    QList<Promotion> running;
    for (const Promotion &p : *all) {
        if (AguaraPromotion::runsOn(p, day))
            running << p;
    }
    if (running.isEmpty())
        return running;                 // what nearly every scan finds: no more queries

    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT marca_id, rubro_id FROM productos WHERE codigo = ?"));
    q.addBindValue(code);
    if (!exec(q))
        return std::nullopt;
    if (!q.next())
        return QList<Promotion>();
    const qint64 brand = q.value(0).toLongLong();
    const qint64 category = q.value(1).toLongLong();
    const std::optional<QHash<qint64, qint64>> parents = categoryParents();
    if (!parents)
        return std::nullopt;
    const QList<qint64> categories = withParents(category, *parents);

    QList<Promotion> offers;
    for (const Promotion &p : qAsConst(running)) {
        if (AguaraPromotion::covers(p, code, brand, categories))
            offers << p;
    }
    return offers;
}

QList<Product> SqlPromotionService::articlesIn(const Promotion &promotion) const
{
    QList<Product> result;
    const std::optional<QHash<qint64, qint64>> parents = categoryParents();
    if (!parents)
        return result;
    QSqlQuery q(db());
    q.setForwardOnly(true);
    q.prepare(QStringLiteral(
        "SELECT p.codigo, p.descripcion, p.precio, p.marca_id, m.nombre, p.rubro_id, r.rubro"
        " FROM productos p"
        " LEFT JOIN marcas m ON m.id = p.marca_id"
        " LEFT JOIN rubros r ON r.id = p.rubro_id"
        " WHERE p.es_activo = 1"));
    if (!exec(q))
        return result;
    QHash<qint64, QList<qint64>> lines;         // a catalog has few categories and many articles
    while (q.next()) {
        const qint64 category = q.value(5).toLongLong();
        auto line = lines.find(category);
        if (line == lines.end())
            line = lines.insert(category, withParents(category, *parents));
        if (!AguaraPromotion::covers(promotion, q.value(0).toString(), q.value(3).toLongLong(), line.value()))
            continue;
        Product p;
        p.code = q.value(0).toString();
        p.description = q.value(1).toString();
        p.price = q.value(2).toLongLong();
        p.brandId = q.value(3).toLongLong();
        p.brandName = q.value(4).toString();
        p.categoryId = category;
        p.categoryName = q.value(6).toString();
        result << p;
    }
    std::sort(result.begin(), result.end(), [](const Product &a, const Product &b) {
        return a.description != b.description ? a.description < b.description : a.code < b.code;
    });
    return result;
}
