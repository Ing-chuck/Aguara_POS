#include "sqlreportservice.h"
#include "connectionmonitor.h"
#include "reportpainter.h"
#include "telemetry.h"
#include "sqlproductservice.h"

#include <cashservice.h>
#include <inventoryservice.h>
#include <returnservice.h>
#include <salesservice.h>
#include <userservice.h>

#include <QFile>
#include <QHash>
#include <QMap>
#include <QMarginsF>
#include <QPageLayout>
#include <QPageSize>
#include <QPrinter>
#include <QSaveFile>
#include <QSet>
#include <QSqlQuery>

#include <algorithm>

namespace {

using Row = QVector<QVariant>;
const char *const kCosts = "reportes.costos";
const char *const kAudit = "reportes.auditoria";

double percent(Money part, Money whole)
{
    return whole == 0 ? 0.0 : 100.0 * double(part) / double(whole);
}

/// Most money first. Ties by name: the same order every time.
void sortByAmount(QList<Row> *rows, int amountColumn)
{
    std::sort(rows->begin(), rows->end(), [amountColumn](const Row &a, const Row &b) {
        const Money x = a.at(amountColumn).toLongLong();
        const Money y = b.at(amountColumn).toLongLong();
        return x != y ? x > y : a.at(0).toString() < b.at(0).toString();
    });
}

ReportDefinition report(const QString &id, const QString &title, const QString &group, int filters,
                        const QString &permission = QString(), const QString &textLabel = QString())
{
    ReportDefinition d;
    d.id = id;
    d.title = title;
    d.group = group;
    d.filters = filters;
    d.permission = permission;
    d.textLabel = textLabel;
    return d;
}

QString movementName(const QString &type)
{
    if (type == QLatin1String(CashMovementType::In))
        return QObject::tr("Ingreso");
    return type == QLatin1String(CashMovementType::Out) ? QObject::tr("Egreso") : QObject::tr("Retiro");
}

} // namespace

SqlReportService::SqlReportService(ConnectionMonitor *monitor, SqlProductService *products, QObject *parent)
    : QObject(parent), m_monitor(monitor), m_products(products), m_connectionName(monitor->connectionName())
{
}

QSqlDatabase SqlReportService::db() const
{
    return QSqlDatabase::database(m_connectionName, false);
}

bool SqlReportService::run(QSqlQuery &q, const QString &sql, const QVariantList &values) const
{
    q.prepare(sql);
    for (const QVariant &value : values)
        q.addBindValue(value);
    if (m_monitor->exec(q))
        return true;
    m_lastError = m_monitor->lastError();
    return false;
}

// --- the list ----------------------------------------------------------------

const QList<SqlReportService::Entry> &SqlReportService::entries()
{
    using namespace ReportFilters;
    static const QString sales = tr("Ventas"), till = tr("Caja"), stock = tr("Inventario"), tax = tr("Impositivo");
    static const QString audit = tr("Auditoría"), costs = QString::fromLatin1(kCosts);
    static const QList<Entry> list = {
        { report(QStringLiteral("ventas.periodo"), tr("Ventas por día"), sales, Dates | Register | User), &SqlReportService::salesByDay },
        { report(QStringLiteral("ventas.hora"), tr("Ventas por hora"), sales, Dates | Register | User), &SqlReportService::salesByHour },
        { report(QStringLiteral("ventas.articulo"), tr("Ventas por artículo"), sales, Dates | Register | User | Category), &SqlReportService::salesByArticle },
        { report(QStringLiteral("ventas.rubro"), tr("Ventas por rubro"), sales, Dates | Register | User | Category), &SqlReportService::salesByCategory },
        { report(QStringLiteral("ventas.marca"), tr("Ventas por marca"), sales, Dates | Register | User | Category), &SqlReportService::salesByBrand },
        { report(QStringLiteral("ventas.cajero"), tr("Ventas por cajero"), sales, Dates | Register), &SqlReportService::salesByCashier },
        { report(QStringLiteral("ventas.medio"), tr("Ventas por medio de pago"), sales, Dates | Register | User), &SqlReportService::salesByMethod },
        { report(QStringLiteral("ventas.promocion"), tr("Ventas por promoción"), sales, Dates | Register | User), &SqlReportService::salesByPromotion },
        { report(QStringLiteral("margen.articulo"), tr("Margen por artículo"), sales, Dates | Register | User | Category, costs), &SqlReportService::marginByArticle },
        { report(QStringLiteral("margen.rubro"), tr("Margen por rubro"), sales, Dates | Register | User | Category, costs), &SqlReportService::marginByCategory },
        { report(QStringLiteral("caja.sesiones"), tr("Sesiones de caja"), till, Dates | Register | User), &SqlReportService::cashSessions },
        { report(QStringLiteral("caja.movimientos"), tr("Movimientos de efectivo"), till, Dates | Register | User), &SqlReportService::cashMovements },
        { report(QStringLiteral("caja.anulaciones"), tr("Anulaciones y devoluciones"), till, Dates | Register), &SqlReportService::voidsAndReturns },
        { report(QStringLiteral("stock.valorizado"), tr("Stock valorizado"), stock, Category, costs), &SqlReportService::stockValue },
        { report(QStringLiteral("stock.minimo"), tr("Bajo el mínimo"), stock, Category), &SqlReportService::stockLow },
        { report(QStringLiteral("stock.sin_movimiento"), tr("Sin movimiento"), stock, Category | Days), &SqlReportService::stockIdle },
        { report(QStringLiteral("stock.kardex"), tr("Kardex de un artículo"), stock, Dates | Text, QString(), tr("Código del artículo")), &SqlReportService::kardex },
        { report(QStringLiteral("stock.compras"), tr("Compras por proveedor"), stock, Dates, costs), &SqlReportService::purchasesBySupplier },
        { report(QStringLiteral("stock.ajustes"), tr("Ajustes de stock"), stock, Dates | User | Category), &SqlReportService::adjustments },
        { report(QStringLiteral("iva.resumen"), tr("Resumen de IVA"), tax, Dates | Register), &SqlReportService::vatSummary },
        { report(QStringLiteral("iva.libro"), tr("Libro de ventas (borrador)"), tax, Dates | Register), &SqlReportService::salesBook },
        { report(QStringLiteral("auditoria"), tr("Registro de auditoría"), audit, Dates | User | Text, QString::fromLatin1(kAudit), tr("Acción o texto")), &SqlReportService::auditLog },
    };
    return list;
}

QList<ReportDefinition> SqlReportService::reports() const
{
    QList<ReportDefinition> result;
    for (const Entry &entry : entries())
        result << entry.definition;
    for (ReportProvider *provider : m_providers)
        result += provider->reports();
    return result;
}

QString SqlReportService::describe(const ReportFilter &f, int filters) const
{
    QStringList parts;
    const QString format = QStringLiteral("dd/MM/yyyy");
    if ((filters & ReportFilters::Dates) && f.from.isValid())
        parts << tr("Del %1 al %2").arg(f.from.toString(format), f.to.toString(format));
    if (filters & ReportFilters::Register) {
        for (const ReportChoice &c : registers()) {
            if (c.id == f.registerId)
                parts << c.name;
        }
    }
    if ((filters & ReportFilters::User) && !f.user.isEmpty())
        parts << tr("Usuario %1").arg(f.user);
    if (filters & ReportFilters::Category) {
        for (const ReportChoice &c : categories()) {
            if (c.id == f.categoryId)
                parts << c.name;
        }
    }
    if (filters & ReportFilters::Days)
        parts << tr("Últimos %1 días").arg(f.days);
    if ((filters & ReportFilters::Text) && !f.text.isEmpty())
        parts << f.text;
    return parts.join(QStringLiteral(" · "));
}

std::optional<ReportResult> SqlReportService::run(const QString &id, const ReportFilter &filter)
{
    Telemetry::count(QStringLiteral("reporte.") + id);
    ReportResult result;
    for (const Entry &entry : entries()) {
        if (entry.definition.id != id)
            continue;
        result.title = entry.definition.title;
        result.subtitle = describe(filter, entry.definition.filters);
        if (!(this->*entry.build)(filter, &result))
            return std::nullopt;
        return result;
    }
    for (ReportProvider *provider : m_providers) {
        for (const ReportDefinition &definition : provider->reports()) {
            if (definition.id != id)
                continue;
            result.title = definition.title;
            result.subtitle = describe(filter, definition.filters);
            if (!provider->runReport(id, filter, &result, &m_lastError))
                return std::nullopt;
            return result;
        }
    }
    m_lastError = tr("Ese reporte no existe.");
    return std::nullopt;
}

QList<ReportChoice> SqlReportService::registers() const
{
    QList<ReportChoice> result;
    QSqlQuery q(db());
    if (run(q, QStringLiteral("SELECT id, nombre FROM cajas ORDER BY nombre"), {})) {
        while (q.next())
            result << ReportChoice{ q.value(0).toLongLong(), q.value(1).toString() };
    }
    return result;
}

QList<ReportChoice> SqlReportService::sellers() const
{
    QList<ReportChoice> result;
    QSqlQuery q(db());
    if (run(q, QStringLiteral("SELECT DISTINCT usuario FROM ventas WHERE usuario IS NOT NULL AND usuario <> '' ORDER BY usuario"), {})) {
        while (q.next())
            result << ReportChoice{ 0, q.value(0).toString() };
    }
    return result;
}

QList<ReportChoice> SqlReportService::categories() const
{
    QList<ReportChoice> result;
    for (const NamedItem &item : m_products->categories())
        result << ReportChoice{ item.id, item.path };
    return result;
}

// --- filters -----------------------------------------------------------------

/// SQLite keeps dates as text, and compares them as text.
QVariant SqlReportService::timeValue(const QDateTime &when) const
{
    if (m_monitor->isPostgres())
        return when;
    return when.toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss"));
}

QString SqlReportService::dateClause(const QString &column, const ReportFilter &f, QVariantList *values) const
{
    QString clause;
    if (f.from.isValid()) {
        clause += QStringLiteral(" AND %1 >= ?").arg(column);
        *values << timeValue(f.from.startOfDay());
    }
    if (f.to.isValid()) {
        clause += QStringLiteral(" AND %1 < ?").arg(column);
        *values << timeValue(f.to.addDays(1).startOfDay());
    }
    return clause;
}

/// Dates, register and user of something with those columns.
QString SqlReportService::saleClause(const QString &alias, const QString &userColumn, const ReportFilter &f, QVariantList *values) const
{
    QString clause = dateClause(alias + QStringLiteral(".fecha"), f, values);
    if (f.registerId != 0) {
        clause += QStringLiteral(" AND %1.caja_id = ?").arg(alias);
        *values << f.registerId;
    }
    if (!f.user.isEmpty()) {
        clause += QStringLiteral(" AND %1.%2 = ?").arg(alias, userColumn);
        *values << f.user;
    }
    return clause;
}

/// `p.rubro_id` in the category or below it. Ids of our own: safe in the text.
QString SqlReportService::categoryClause(const ReportFilter &f) const
{
    if (f.categoryId == 0)
        return QString();
    QSet<qint64> ids = { f.categoryId };
    const QList<NamedItem> all = m_products->categories();
    for (bool grew = true; grew;) {
        grew = false;
        for (const NamedItem &item : all) {
            if (ids.contains(item.parentId) && !ids.contains(item.id)) {
                ids.insert(item.id);
                grew = true;
            }
        }
    }
    QStringList list;
    for (qint64 id : qAsConst(ids))
        list << QString::number(id);
    return QStringLiteral(" AND p.rubro_id IN (%1)").arg(list.join(QLatin1Char(',')));
}

/// What was sold in the period, and what came back in it. Returns count on the
/// day they happened, at the cost of their sale. Without cash rounding: that
/// belongs to the ticket, not to an article.
bool SqlReportService::lines(const ReportFilter &f, QList<Line> *out)
{
    const QString articles = QStringLiteral(
        " LEFT JOIN productos p ON p.codigo = i.producto_codigo"
        " LEFT JOIN rubros r ON r.id = p.rubro_id LEFT JOIN marcas m ON m.id = p.marca_id");
    const QString category = categoryClause(f);

    QVariantList values = { QString::fromLatin1(SaleState::Paid) };
    const QString sold = saleClause(QStringLiteral("v"), QStringLiteral("usuario"), f, &values);
    QSqlQuery q(db());
    if (!run(q, QStringLiteral(
            "SELECT i.producto_codigo, i.descripcion, r.rubro, m.nombre, v.usuario, i.cantidad, i.importe, i.costo_unitario"
            " FROM ventas_items i JOIN ventas v ON v.id = i.venta_id") + articles
            + QStringLiteral(" WHERE v.estado = ?") + sold + category, values))
        return false;
    while (q.next()) {
        Line line;
        line.code = q.value(0).toString();
        line.description = q.value(1).toString();
        line.category = q.value(2).toString();
        line.brand = q.value(3).toString();
        line.user = q.value(4).toString();
        line.quantity = q.value(5).toLongLong();
        line.amount = q.value(6).toLongLong();
        line.cost = AguaraQuantity::lineTotal(q.value(7).toLongLong(), line.quantity);
        *out << line;
    }

    values.clear();
    const QString returned = saleClause(QStringLiteral("d"), QStringLiteral("usuario"), f, &values);
    QSqlQuery back(db());
    if (!run(back, QStringLiteral(
            "SELECT i.producto_codigo, i.descripcion, r.rubro, m.nombre, d.usuario, x.cantidad, x.importe, i.costo_unitario"
            " FROM devoluciones_items x JOIN devoluciones d ON d.id = x.devolucion_id"
            " JOIN ventas_items i ON i.venta_id = d.venta_id AND i.linea = x.linea") + articles
            + QStringLiteral(" WHERE 1 = 1") + returned + category, values))
        return false;
    while (back.next()) {
        Line line;
        line.code = back.value(0).toString();
        line.description = back.value(1).toString();
        line.category = back.value(2).toString();
        line.brand = back.value(3).toString();
        line.user = back.value(4).toString();
        line.quantity = -back.value(5).toLongLong();
        line.amount = -back.value(6).toLongLong();
        line.cost = -AguaraQuantity::lineTotal(back.value(7).toLongLong(), back.value(5).toLongLong());
        *out << line;
    }
    return true;
}

// --- sales -------------------------------------------------------------------

bool SqlReportService::salesByDay(const ReportFilter &f, ReportResult *r)
{
    struct Day { qint64 tickets = 0; Money discounts = 0, sold = 0, returned = 0; };
    QMap<QDate, Day> days;

    QVariantList values = { QString::fromLatin1(SaleState::Paid) };
    QSqlQuery q(db());
    if (!run(q, QStringLiteral("SELECT v.fecha, v.descuento, v.total FROM ventas v WHERE v.estado = ?")
                    + saleClause(QStringLiteral("v"), QStringLiteral("usuario"), f, &values), values))
        return false;
    while (q.next()) {
        Day &day = days[q.value(0).toDateTime().date()];
        ++day.tickets;
        day.discounts += q.value(1).toLongLong();
        day.sold += q.value(2).toLongLong();
    }
    values.clear();
    QSqlQuery back(db());
    if (!run(back, QStringLiteral("SELECT d.fecha, d.pagado FROM devoluciones d WHERE 1 = 1")
                       + saleClause(QStringLiteral("d"), QStringLiteral("usuario"), f, &values), values))
        return false;
    while (back.next())
        days[back.value(0).toDateTime().date()].returned += back.value(1).toLongLong();

    r->columns = { { tr("Fecha"), ReportCell::Date }, { tr("Tickets"), ReportCell::Integer }, { tr("Descuentos"), ReportCell::Money },
                   { tr("Vendido"), ReportCell::Money }, { tr("Devoluciones"), ReportCell::Money },
                   { tr("Neto"), ReportCell::Money }, { tr("Ticket promedio"), ReportCell::Money } };
    Day all;
    for (auto it = days.cbegin(); it != days.cend(); ++it) {
        const Day &d = it.value();
        r->rows << Row{ it.key(), d.tickets, d.discounts, d.sold, d.returned, d.sold - d.returned,
                        d.tickets ? d.sold / d.tickets : 0 };
        all.tickets += d.tickets;
        all.discounts += d.discounts;
        all.sold += d.sold;
        all.returned += d.returned;
    }
    r->totals = Row{ tr("Total"), all.tickets, all.discounts, all.sold, all.returned, all.sold - all.returned,
                     all.tickets ? all.sold / all.tickets : 0 };
    r->note = tr("Vendido es lo cobrado, y coincide con las planillas de cierre. Las ventas anuladas no cuentan.");
    return true;
}

bool SqlReportService::salesByHour(const ReportFilter &f, ReportResult *r)
{
    QMap<int, QPair<qint64, Money>> hours;
    QVariantList values = { QString::fromLatin1(SaleState::Paid) };
    QSqlQuery q(db());
    if (!run(q, QStringLiteral("SELECT v.fecha, v.total FROM ventas v WHERE v.estado = ?")
                    + saleClause(QStringLiteral("v"), QStringLiteral("usuario"), f, &values), values))
        return false;
    Money total = 0;
    qint64 tickets = 0;
    while (q.next()) {
        auto &hour = hours[q.value(0).toDateTime().time().hour()];
        ++hour.first;
        hour.second += q.value(1).toLongLong();
        ++tickets;
        total += q.value(1).toLongLong();
    }
    r->columns = { { tr("Hora"), ReportCell::Text }, { tr("Tickets"), ReportCell::Integer },
                   { tr("Vendido"), ReportCell::Money }, { tr("% del total"), ReportCell::Percent } };
    for (auto it = hours.cbegin(); it != hours.cend(); ++it)
        r->rows << Row{ QStringLiteral("%1:00").arg(it.key(), 2, 10, QLatin1Char('0')), it.value().first, it.value().second,
                        percent(it.value().second, total) };
    r->totals = Row{ tr("Total"), tickets, total, QVariant() };
    return true;
}

/// What each promotion sold and what it gave, by the name each one had: net of
/// returns, which give back their share of the discount.
bool SqlReportService::salesByPromotion(const ReportFilter &f, ReportResult *r)
{
    struct Promo { QSet<qint64> tickets; Quantity lines = 0; Money given = 0, sold = 0; };
    QMap<QString, Promo> promotions;

    QVariantList values = { QString::fromLatin1(SaleState::Paid) };
    const QString soldClause = saleClause(QStringLiteral("v"), QStringLiteral("usuario"), f, &values);
    QSqlQuery q(db());
    if (!run(q, QStringLiteral(
            "SELECT i.promocion, v.id, i.descuento_promocion, i.importe"
            " FROM ventas_items i JOIN ventas v ON v.id = i.venta_id"
            " WHERE i.promocion_id IS NOT NULL AND v.estado = ?") + soldClause, values))
        return false;
    while (q.next()) {
        Promo &promo = promotions[q.value(0).toString()];
        promo.tickets.insert(q.value(1).toLongLong());
        ++promo.lines;
        promo.given += q.value(2).toLongLong();
        promo.sold += q.value(3).toLongLong();
    }

    values.clear();
    const QString returned = saleClause(QStringLiteral("d"), QStringLiteral("usuario"), f, &values);
    QSqlQuery back(db());
    if (!run(back, QStringLiteral(
            "SELECT i.promocion, i.descuento_promocion, i.cantidad, x.cantidad, x.importe"
            " FROM devoluciones_items x JOIN devoluciones d ON d.id = x.devolucion_id"
            " JOIN ventas_items i ON i.venta_id = d.venta_id AND i.linea = x.linea"
            " WHERE i.promocion_id IS NOT NULL") + returned, values))
        return false;
    while (back.next()) {
        Promo &promo = promotions[back.value(0).toString()];
        const Quantity whole = back.value(2).toLongLong();
        if (whole > 0)
            promo.given -= back.value(1).toLongLong() * back.value(3).toLongLong() / whole;
        promo.sold -= back.value(4).toLongLong();
    }

    r->columns = { { tr("Promoción"), ReportCell::Text }, { tr("Tickets"), ReportCell::Integer },
                   { tr("Líneas"), ReportCell::Integer }, { tr("Descuento dado"), ReportCell::Money },
                   { tr("Vendido"), ReportCell::Money } };
    Money given = 0, sold = 0;
    for (auto it = promotions.cbegin(); it != promotions.cend(); ++it) {
        const Promo &promo = it.value();
        r->rows << Row{ it.key(), promo.tickets.size(), promo.lines, promo.given, promo.sold };
        given += promo.given;
        sold += promo.sold;
    }
    sortByAmount(&r->rows, 4);
    r->totals = Row{ tr("Total"), QVariant(), QVariant(), given, sold };
    r->note = tr("Neto de devoluciones y descuentos, sin el redondeo del efectivo.");
    return true;
}

bool SqlReportService::salesByArticle(const ReportFilter &f, ReportResult *r)
{
    QList<Line> all;
    if (!lines(f, &all))
        return false;
    QHash<QString, Line> articles;
    Money total = 0;
    for (const Line &line : qAsConst(all)) {
        Line &article = articles[line.code];
        if (article.code.isEmpty()) {
            article.code = line.code;
            article.description = line.description;
        }
        article.quantity += line.quantity;
        article.amount += line.amount;
        total += line.amount;
    }
    r->columns = { { tr("Código"), ReportCell::Text }, { tr("Artículo"), ReportCell::Text }, { tr("Cantidad"), ReportCell::Quantity },
                   { tr("Importe"), ReportCell::Money }, { tr("% del total"), ReportCell::Percent } };
    for (const Line &article : qAsConst(articles))
        r->rows << Row{ article.code, article.description, article.quantity, article.amount, percent(article.amount, total) };
    sortByAmount(&r->rows, 3);
    r->totals = Row{ tr("Total"), QVariant(), QVariant(), total, QVariant() };
    r->note = tr("Neto de devoluciones y descuentos, sin el redondeo del efectivo.");
    return true;
}

bool SqlReportService::salesByCategory(const ReportFilter &f, ReportResult *r)
{
    QList<Line> all;
    if (!lines(f, &all))
        return false;
    QHash<QString, Money> groups;
    Money total = 0;
    for (const Line &line : qAsConst(all)) {
        groups[line.category.isEmpty() ? tr("(sin rubro)") : line.category] += line.amount;
        total += line.amount;
    }
    r->columns = { { tr("Rubro"), ReportCell::Text }, { tr("Importe"), ReportCell::Money }, { tr("% del total"), ReportCell::Percent } };
    for (auto it = groups.cbegin(); it != groups.cend(); ++it)
        r->rows << Row{ it.key(), it.value(), percent(it.value(), total) };
    sortByAmount(&r->rows, 1);
    r->totals = Row{ tr("Total"), total, QVariant() };
    r->note = tr("Neto de devoluciones y descuentos, sin el redondeo del efectivo.");
    return true;
}

bool SqlReportService::salesByBrand(const ReportFilter &f, ReportResult *r)
{
    QList<Line> all;
    if (!lines(f, &all))
        return false;
    QHash<QString, Money> groups;
    Money total = 0;
    for (const Line &line : qAsConst(all)) {
        groups[line.brand.isEmpty() ? tr("(sin marca)") : line.brand] += line.amount;
        total += line.amount;
    }
    r->columns = { { tr("Marca"), ReportCell::Text }, { tr("Importe"), ReportCell::Money }, { tr("% del total"), ReportCell::Percent } };
    for (auto it = groups.cbegin(); it != groups.cend(); ++it)
        r->rows << Row{ it.key(), it.value(), percent(it.value(), total) };
    sortByAmount(&r->rows, 1);
    r->totals = Row{ tr("Total"), total, QVariant() };
    r->note = tr("Neto de devoluciones y descuentos, sin el redondeo del efectivo.");
    return true;
}

bool SqlReportService::salesByCashier(const ReportFilter &f, ReportResult *r)
{
    struct Cashier { qint64 tickets = 0; Money sold = 0, returned = 0; };
    QHash<QString, Cashier> cashiers;
    ReportFilter any = f;
    any.user.clear();
    QVariantList values = { QString::fromLatin1(SaleState::Paid) };
    QSqlQuery q(db());
    if (!run(q, QStringLiteral("SELECT v.usuario, v.total FROM ventas v WHERE v.estado = ?")
                    + saleClause(QStringLiteral("v"), QStringLiteral("usuario"), any, &values), values))
        return false;
    while (q.next()) {
        Cashier &c = cashiers[q.value(0).toString()];
        ++c.tickets;
        c.sold += q.value(1).toLongLong();
    }
    values.clear();
    QSqlQuery back(db());
    if (!run(back, QStringLiteral("SELECT d.usuario, d.pagado FROM devoluciones d WHERE 1 = 1")
                       + saleClause(QStringLiteral("d"), QStringLiteral("usuario"), any, &values), values))
        return false;
    while (back.next())
        cashiers[back.value(0).toString()].returned += back.value(1).toLongLong();

    r->columns = { { tr("Cajero"), ReportCell::Text }, { tr("Tickets"), ReportCell::Integer }, { tr("Vendido"), ReportCell::Money },
                   { tr("Devoluciones"), ReportCell::Money }, { tr("Neto"), ReportCell::Money } };
    Cashier all;
    for (auto it = cashiers.cbegin(); it != cashiers.cend(); ++it) {
        const Cashier &c = it.value();
        r->rows << Row{ it.key(), c.tickets, c.sold, c.returned, c.sold - c.returned };
        all.tickets += c.tickets;
        all.sold += c.sold;
        all.returned += c.returned;
    }
    sortByAmount(&r->rows, 4);
    r->totals = Row{ tr("Total"), all.tickets, all.sold, all.returned, all.sold - all.returned };
    return true;
}

bool SqlReportService::salesByMethod(const ReportFilter &f, ReportResult *r)
{
    QHash<QString, QPair<Money, Money>> methods;
    QVariantList values = { QString::fromLatin1(SaleState::Paid) };
    QSqlQuery q(db());
    if (!run(q, QStringLiteral("SELECT p.medio, p.monto, p.vuelto FROM ventas_pagos p JOIN ventas v ON v.id = p.venta_id"
                               " WHERE v.estado = ?") + saleClause(QStringLiteral("v"), QStringLiteral("usuario"), f, &values), values))
        return false;
    while (q.next())
        methods[q.value(0).toString()].first += q.value(1).toLongLong() - q.value(2).toLongLong();
    values.clear();
    QSqlQuery back(db());
    if (!run(back, QStringLiteral("SELECT d.medio, d.pagado FROM devoluciones d WHERE 1 = 1")
                       + saleClause(QStringLiteral("d"), QStringLiteral("usuario"), f, &values), values))
        return false;
    while (back.next())
        methods[back.value(0).toString()].second += back.value(1).toLongLong();

    r->columns = { { tr("Medio de pago"), ReportCell::Text }, { tr("Cobrado"), ReportCell::Money },
                   { tr("Devuelto"), ReportCell::Money }, { tr("Neto"), ReportCell::Money } };
    Money in = 0, out = 0;
    for (auto it = methods.cbegin(); it != methods.cend(); ++it) {
        r->rows << Row{ AguaraSale::methodName(it.key()), it.value().first, it.value().second, it.value().first - it.value().second };
        in += it.value().first;
        out += it.value().second;
    }
    sortByAmount(&r->rows, 3);
    r->totals = Row{ tr("Total"), in, out, in - out };
    r->note = tr("Nota de crédito: en Cobrado, las usadas como pago; en Devuelto, las emitidas.");
    return true;
}

bool SqlReportService::marginByArticle(const ReportFilter &f, ReportResult *r)
{
    QList<Line> all;
    if (!lines(f, &all))
        return false;
    QHash<QString, Line> articles;
    for (const Line &line : qAsConst(all)) {
        Line &article = articles[line.code];
        if (article.code.isEmpty()) {
            article.code = line.code;
            article.description = line.description;
        }
        article.quantity += line.quantity;
        article.amount += line.amount;
        article.cost += line.cost;
    }
    r->columns = { { tr("Código"), ReportCell::Text }, { tr("Artículo"), ReportCell::Text }, { tr("Cantidad"), ReportCell::Quantity },
                   { tr("Ventas"), ReportCell::Money }, { tr("Costo"), ReportCell::Money }, { tr("Margen"), ReportCell::Money },
                   { tr("% margen"), ReportCell::Percent } };
    Money sales = 0, costs = 0;
    for (const Line &a : qAsConst(articles)) {
        r->rows << Row{ a.code, a.description, a.quantity, a.amount, a.cost, a.amount - a.cost, percent(a.amount - a.cost, a.amount) };
        sales += a.amount;
        costs += a.cost;
    }
    sortByAmount(&r->rows, 5);
    r->totals = Row{ tr("Total"), QVariant(), QVariant(), sales, costs, sales - costs, percent(sales - costs, sales) };
    r->note = tr("Con el costo del momento de cada venta, IVA incluido en ventas y en costo. Un artículo sin costo cargado figura con todo de margen.");
    return true;
}

bool SqlReportService::marginByCategory(const ReportFilter &f, ReportResult *r)
{
    QList<Line> all;
    if (!lines(f, &all))
        return false;
    QHash<QString, QPair<Money, Money>> groups;
    for (const Line &line : qAsConst(all)) {
        auto &group = groups[line.category.isEmpty() ? tr("(sin rubro)") : line.category];
        group.first += line.amount;
        group.second += line.cost;
    }
    r->columns = { { tr("Rubro"), ReportCell::Text }, { tr("Ventas"), ReportCell::Money }, { tr("Costo"), ReportCell::Money },
                   { tr("Margen"), ReportCell::Money }, { tr("% margen"), ReportCell::Percent } };
    Money sales = 0, costs = 0;
    for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
        const Money margin = it.value().first - it.value().second;
        r->rows << Row{ it.key(), it.value().first, it.value().second, margin, percent(margin, it.value().first) };
        sales += it.value().first;
        costs += it.value().second;
    }
    sortByAmount(&r->rows, 3);
    r->totals = Row{ tr("Total"), sales, costs, sales - costs, percent(sales - costs, sales) };
    r->note = tr("Con el costo del momento de cada venta, IVA incluido en ventas y en costo.");
    return true;
}

// --- the till ----------------------------------------------------------------

bool SqlReportService::cashSessions(const ReportFilter &f, ReportResult *r)
{
    QVariantList values;
    QString where = dateClause(QStringLiteral("s.apertura"), f, &values);
    if (f.registerId != 0) {
        where += QStringLiteral(" AND s.caja_id = ?");
        values << f.registerId;
    }
    if (!f.user.isEmpty()) {
        where += QStringLiteral(" AND (s.usuario_apertura = ? OR s.usuario_cierre = ?)");
        values << f.user << f.user;
    }
    QSqlQuery q(db());
    if (!run(q, QStringLiteral(
            "SELECT c.nombre, s.apertura, s.usuario_apertura, s.cierre, s.usuario_cierre, s.fondo, s.estado,"
            " s.esperado, s.contado, s.diferencia, s.motivo, s.autorizado_por"
            " FROM sesiones_caja s JOIN cajas c ON c.id = s.caja_id WHERE 1 = 1") + where + QStringLiteral(" ORDER BY s.apertura DESC"), values))
        return false;
    r->columns = { { tr("Caja"), ReportCell::Text }, { tr("Apertura"), ReportCell::DateTime }, { tr("Abrió"), ReportCell::Text },
                   { tr("Cierre"), ReportCell::DateTime }, { tr("Cerró"), ReportCell::Text }, { tr("Fondo"), ReportCell::Money },
                   { tr("Esperado"), ReportCell::Money }, { tr("Contado"), ReportCell::Money }, { tr("Diferencia"), ReportCell::Money },
                   { tr("Motivo"), ReportCell::Text }, { tr("Autorizó"), ReportCell::Text } };
    Money differences = 0;
    while (q.next()) {
        const bool closed = q.value(6).toString() != QLatin1String("abierta");
        r->rows << Row{ q.value(0), q.value(1).toDateTime(), q.value(2), closed ? QVariant(q.value(3).toDateTime()) : QVariant(), q.value(4),
                        q.value(5).toLongLong(), closed ? q.value(7) : QVariant(), closed ? q.value(8) : QVariant(),
                        closed ? q.value(9) : QVariant(), q.value(10), q.value(11) };
        if (closed)
            differences += q.value(9).toLongLong();
    }
    r->totals = Row{ tr("Total"), QVariant(), QVariant(), QVariant(), QVariant(), QVariant(), QVariant(), QVariant(), differences, QVariant(), QVariant() };
    r->note = tr("Una sesión abierta no muestra lo esperado: el arqueo es a ciegas.");
    return true;
}

bool SqlReportService::cashMovements(const ReportFilter &f, ReportResult *r)
{
    QVariantList values;
    QString where = dateClause(QStringLiteral("m.fecha"), f, &values);
    if (f.registerId != 0) {
        where += QStringLiteral(" AND s.caja_id = ?");
        values << f.registerId;
    }
    if (!f.user.isEmpty()) {
        where += QStringLiteral(" AND m.usuario = ?");
        values << f.user;
    }
    QSqlQuery q(db());
    if (!run(q, QStringLiteral(
            "SELECT m.fecha, c.nombre, m.tipo, m.monto, m.motivo, m.usuario FROM caja_movimientos m"
            " JOIN sesiones_caja s ON s.id = m.sesion_id JOIN cajas c ON c.id = s.caja_id WHERE 1 = 1") + where
            + QStringLiteral(" ORDER BY m.fecha DESC"), values))
        return false;
    r->columns = { { tr("Fecha"), ReportCell::DateTime }, { tr("Caja"), ReportCell::Text }, { tr("Tipo"), ReportCell::Text },
                   { tr("Monto"), ReportCell::Money }, { tr("Motivo"), ReportCell::Text }, { tr("Usuario"), ReportCell::Text } };
    Money total = 0;
    while (q.next()) {
        const QString type = q.value(2).toString();
        const Money amount = type == QLatin1String(CashMovementType::In) ? q.value(3).toLongLong() : -q.value(3).toLongLong();
        r->rows << Row{ q.value(0).toDateTime(), q.value(1), movementName(type), amount, q.value(4), q.value(5) };
        total += amount;
    }
    r->totals = Row{ tr("Total"), QVariant(), QVariant(), total, QVariant(), QVariant() };
    return true;
}

bool SqlReportService::voidsAndReturns(const ReportFilter &f, ReportResult *r)
{
    QVariantList values = { QString::fromLatin1(SaleState::Voided) };
    QString where = dateClause(QStringLiteral("v.anulada_fecha"), f, &values);
    if (f.registerId != 0) {
        where += QStringLiteral(" AND v.caja_id = ?");
        values << f.registerId;
    }
    QSqlQuery q(db());
    if (!run(q, QStringLiteral("SELECT v.anulada_fecha, v.caja_id, v.numero, v.total, v.motivo, v.usuario, v.anulada_por"
                               " FROM ventas v WHERE v.estado = ?") + where, values))
        return false;
    QList<Row> rows;
    Money total = 0;
    while (q.next()) {
        rows << Row{ q.value(0).toDateTime(), tr("Anulación"), AguaraSale::formatNumber(q.value(1).toLongLong(), q.value(2).toLongLong()),
                     q.value(3).toLongLong(), QString(), q.value(4), q.value(5), q.value(6) };
        total += q.value(3).toLongLong();
    }
    values.clear();
    ReportFilter any = f;
    any.user.clear();
    QSqlQuery back(db());
    if (!run(back, QStringLiteral(
            "SELECT d.fecha, v.caja_id, v.numero, d.pagado, d.medio, d.motivo, d.usuario, d.autorizo"
            " FROM devoluciones d JOIN ventas v ON v.id = d.venta_id WHERE 1 = 1")
            + saleClause(QStringLiteral("d"), QStringLiteral("usuario"), any, &values), values))
        return false;
    while (back.next()) {
        rows << Row{ back.value(0).toDateTime(), tr("Devolución"), AguaraSale::formatNumber(back.value(1).toLongLong(), back.value(2).toLongLong()),
                     back.value(3).toLongLong(), AguaraSale::methodName(back.value(4).toString()), back.value(5), back.value(6), back.value(7) };
        total += back.value(3).toLongLong();
    }
    std::sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) { return a.at(0).toDateTime() > b.at(0).toDateTime(); });
    r->rows = rows;
    r->columns = { { tr("Fecha"), ReportCell::DateTime }, { tr("Tipo"), ReportCell::Text }, { tr("Venta"), ReportCell::Text },
                   { tr("Importe"), ReportCell::Money }, { tr("Medio"), ReportCell::Text }, { tr("Motivo"), ReportCell::Text },
                   { tr("Usuario"), ReportCell::Text }, { tr("Autorizó"), ReportCell::Text } };
    r->totals = Row{ tr("Total"), QVariant(), QVariant(), total, QVariant(), QVariant(), QVariant(), QVariant() };
    return true;
}

// --- inventory ---------------------------------------------------------------

bool SqlReportService::stockValue(const ReportFilter &f, ReportResult *r)
{
    QSqlQuery q(db());
    if (!run(q, QStringLiteral("SELECT p.codigo, p.descripcion, r.rubro, p.stock, p.costo, p.precio FROM productos p"
                               " LEFT JOIN rubros r ON r.id = p.rubro_id WHERE p.es_activo = 1")
                    + categoryClause(f) + QStringLiteral(" ORDER BY p.descripcion"), {}))
        return false;
    r->columns = { { tr("Código"), ReportCell::Text }, { tr("Artículo"), ReportCell::Text }, { tr("Rubro"), ReportCell::Text },
                   { tr("Stock"), ReportCell::Quantity }, { tr("Costo"), ReportCell::Money }, { tr("Precio"), ReportCell::Money },
                   { tr("Valor a costo"), ReportCell::Money }, { tr("Valor a precio"), ReportCell::Money } };
    Money atCost = 0, atPrice = 0;
    while (q.next()) {
        const Quantity stock = q.value(3).toLongLong();
        const Money cost = AguaraQuantity::lineTotal(q.value(4).toLongLong(), stock);
        const Money price = AguaraQuantity::lineTotal(q.value(5).toLongLong(), stock);
        r->rows << Row{ q.value(0), q.value(1), q.value(2), stock, q.value(4).toLongLong(), q.value(5).toLongLong(), cost, price };
        atCost += cost;
        atPrice += price;
    }
    r->totals = Row{ tr("Total"), QVariant(), QVariant(), QVariant(), QVariant(), QVariant(), atCost, atPrice };
    r->note = tr("Un stock negativo resta: conviene corregirlo con un recuento.");
    return true;
}

bool SqlReportService::stockLow(const ReportFilter &f, ReportResult *r)
{
    QSqlQuery q(db());
    if (!run(q, QStringLiteral("SELECT p.codigo, p.descripcion, r.rubro, p.stock, p.stock_min FROM productos p"
                               " LEFT JOIN rubros r ON r.id = p.rubro_id WHERE p.es_activo = 1 AND p.stock_min > 0"
                               " AND p.stock <= p.stock_min") + categoryClause(f) + QStringLiteral(" ORDER BY p.descripcion"), {}))
        return false;
    r->columns = { { tr("Código"), ReportCell::Text }, { tr("Artículo"), ReportCell::Text }, { tr("Rubro"), ReportCell::Text },
                   { tr("Stock"), ReportCell::Quantity }, { tr("Mínimo"), ReportCell::Quantity }, { tr("Faltan"), ReportCell::Quantity } };
    while (q.next()) {
        const Quantity stock = q.value(3).toLongLong();
        const Quantity minimum = q.value(4).toLongLong();
        r->rows << Row{ q.value(0), q.value(1), q.value(2), stock, minimum, minimum - stock };
    }
    return true;
}

bool SqlReportService::stockIdle(const ReportFilter &f, ReportResult *r)
{
    const QVariantList values = { timeValue(QDate::currentDate().addDays(-f.days).startOfDay()) };
    QSqlQuery q(db());
    if (!run(q, QStringLiteral(
            "SELECT p.codigo, p.descripcion, r.rubro, p.stock,"
            " (SELECT MAX(m.fecha) FROM stock_movimientos m WHERE m.producto_codigo = p.codigo) AS ultimo"
            " FROM productos p LEFT JOIN rubros r ON r.id = p.rubro_id"
            " WHERE p.es_activo = 1 AND p.stock <> 0 AND NOT EXISTS"
            " (SELECT 1 FROM stock_movimientos m WHERE m.producto_codigo = p.codigo AND m.fecha >= ?)")
            + categoryClause(f) + QStringLiteral(" ORDER BY ultimo, p.descripcion"), values))
        return false;
    r->columns = { { tr("Código"), ReportCell::Text }, { tr("Artículo"), ReportCell::Text }, { tr("Rubro"), ReportCell::Text },
                   { tr("Stock"), ReportCell::Quantity }, { tr("Último movimiento"), ReportCell::DateTime } };
    while (q.next())
        r->rows << Row{ q.value(0), q.value(1), q.value(2), q.value(3).toLongLong(),
                        q.isNull(4) ? QVariant() : QVariant(q.value(4).toDateTime()) };
    r->note = tr("Artículos con stock que no se movieron: ni ventas, ni compras, ni ajustes.");
    return true;
}

bool SqlReportService::kardex(const ReportFilter &f, ReportResult *r)
{
    const QString code = f.text.trimmed();
    const std::optional<Product> product = m_products->product(code);
    if (!product) {
        m_lastError = tr("Escriba el código de un artículo.");
        return false;
    }
    r->subtitle = QStringLiteral("%1 · %2").arg(product->description, r->subtitle);
    QVariantList values = { code };
    QSqlQuery q(db());
    if (!run(q, QStringLiteral(
            "SELECT m.fecha, m.tipo, m.cantidad, m.saldo, m.costo_unitario, m.motivo, m.documento, m.usuario"
            " FROM stock_movimientos m WHERE m.producto_codigo = ?") + dateClause(QStringLiteral("m.fecha"), f, &values)
            + QStringLiteral(" ORDER BY m.id"), values))
        return false;
    r->columns = { { tr("Fecha"), ReportCell::DateTime }, { tr("Tipo"), ReportCell::Text }, { tr("Cantidad"), ReportCell::Quantity },
                   { tr("Saldo"), ReportCell::Quantity }, { tr("Costo"), ReportCell::Money }, { tr("Motivo"), ReportCell::Text },
                   { tr("Documento"), ReportCell::Text }, { tr("Usuario"), ReportCell::Text } };
    while (q.next())
        r->rows << Row{ q.value(0).toDateTime(), q.value(1), q.value(2).toLongLong(), q.value(3).toLongLong(),
                        q.value(4).toLongLong(), q.value(5), q.value(6), q.value(7) };
    return true;
}

bool SqlReportService::purchasesBySupplier(const ReportFilter &f, ReportResult *r)
{
    QVariantList values;
    QString where;
    if (f.from.isValid()) {
        where += QStringLiteral(" AND c.fecha >= ?");
        values << f.from;
    }
    if (f.to.isValid()) {
        where += QStringLiteral(" AND c.fecha <= ?");
        values << f.to;
    }
    QSqlQuery q(db());
    if (!run(q, QStringLiteral("SELECT s.nombre, c.total FROM compras c JOIN proveedores s ON s.id = c.proveedor_id WHERE 1 = 1") + where, values))
        return false;
    QHash<QString, QPair<qint64, Money>> suppliers;
    qint64 count = 0;
    Money total = 0;
    while (q.next()) {
        auto &supplier = suppliers[q.value(0).toString()];
        ++supplier.first;
        supplier.second += q.value(1).toLongLong();
        ++count;
        total += q.value(1).toLongLong();
    }
    r->columns = { { tr("Proveedor"), ReportCell::Text }, { tr("Compras"), ReportCell::Integer }, { tr("Total"), ReportCell::Money } };
    for (auto it = suppliers.cbegin(); it != suppliers.cend(); ++it)
        r->rows << Row{ it.key(), it.value().first, it.value().second };
    sortByAmount(&r->rows, 2);
    r->totals = Row{ tr("Total"), count, total };
    return true;
}

bool SqlReportService::adjustments(const ReportFilter &f, ReportResult *r)
{
    QVariantList values = { QString::fromLatin1(StockMovementType::Adjustment), QString::fromLatin1(StockMovementType::Count) };
    QString where = dateClause(QStringLiteral("x.fecha"), f, &values);
    if (!f.user.isEmpty()) {
        where += QStringLiteral(" AND x.usuario = ?");
        values << f.user;
    }
    QSqlQuery q(db());
    if (!run(q, QStringLiteral(
            "SELECT x.fecha, x.tipo, x.producto_codigo, p.descripcion, x.cantidad, x.costo_unitario, x.motivo, x.usuario"
            " FROM stock_movimientos x JOIN productos p ON p.codigo = x.producto_codigo WHERE x.tipo IN (?, ?)") + where
            + categoryClause(f) + QStringLiteral(" ORDER BY x.id DESC"), values))
        return false;
    r->columns = { { tr("Fecha"), ReportCell::DateTime }, { tr("Tipo"), ReportCell::Text }, { tr("Código"), ReportCell::Text },
                   { tr("Artículo"), ReportCell::Text }, { tr("Cantidad"), ReportCell::Quantity }, { tr("Valor a costo"), ReportCell::Money },
                   { tr("Motivo"), ReportCell::Text }, { tr("Usuario"), ReportCell::Text } };
    Money total = 0;
    while (q.next()) {
        const Quantity quantity = q.value(4).toLongLong();
        const Money value = AguaraQuantity::lineTotal(q.value(5).toLongLong(), quantity);
        r->rows << Row{ q.value(0).toDateTime(), q.value(1), q.value(2), q.value(3), quantity, value, q.value(6), q.value(7) };
        total += value;
    }
    r->totals = Row{ tr("Total"), QVariant(), QVariant(), QVariant(), QVariant(), total, QVariant(), QVariant() };
    return true;
}

// --- taxes -------------------------------------------------------------------

bool SqlReportService::vatSummary(const ReportFilter &f, ReportResult *r)
{
    Money taxed10 = 0, vat10 = 0, taxed5 = 0, vat5 = 0, exempt = 0;
    QVariantList values = { QString::fromLatin1(SaleState::Paid) };
    ReportFilter any = f;
    any.user.clear();
    QSqlQuery q(db());
    if (!run(q, QStringLiteral("SELECT v.gravado10, v.iva10, v.gravado5, v.iva5, v.exento FROM ventas v WHERE v.estado = ?")
                    + saleClause(QStringLiteral("v"), QStringLiteral("usuario"), any, &values), values))
        return false;
    while (q.next()) {
        taxed10 += q.value(0).toLongLong();
        vat10 += q.value(1).toLongLong();
        taxed5 += q.value(2).toLongLong();
        vat5 += q.value(3).toLongLong();
        exempt += q.value(4).toLongLong();
    }
    values.clear();
    QSqlQuery back(db());
    if (!run(back, QStringLiteral(
            "SELECT i.iva_tasa, x.importe, x.iva FROM devoluciones_items x JOIN devoluciones d ON d.id = x.devolucion_id"
            " JOIN ventas_items i ON i.venta_id = d.venta_id AND i.linea = x.linea WHERE 1 = 1")
            + saleClause(QStringLiteral("d"), QStringLiteral("usuario"), any, &values), values))
        return false;
    while (back.next()) {
        const int rate = back.value(0).toInt();
        const Money amount = back.value(1).toLongLong();
        const Money vat = back.value(2).toLongLong();
        if (rate == 10) {
            taxed10 -= amount;
            vat10 -= vat;
        } else if (rate == 5) {
            taxed5 -= amount;
            vat5 -= vat;
        } else {
            exempt -= amount;
        }
    }
    r->columns = { { tr("Tasa"), ReportCell::Text }, { tr("Ventas con IVA"), ReportCell::Money },
                   { tr("IVA"), ReportCell::Money }, { tr("Sin IVA"), ReportCell::Money } };
    r->rows << Row{ tr("10 %"), taxed10, vat10, taxed10 - vat10 }
            << Row{ tr("5 %"), taxed5, vat5, taxed5 - vat5 }
            << Row{ tr("Exentas"), exempt, Money(0), exempt };
    const Money all = taxed10 + taxed5 + exempt;
    r->totals = Row{ tr("Total"), all, vat10 + vat5, all - vat10 - vat5 };
    r->note = tr("Neto de devoluciones. No incluye el redondeo del efectivo. Controlar con el contador: es un resumen interno.");
    return true;
}

bool SqlReportService::salesBook(const ReportFilter &f, ReportResult *r)
{
    QVariantList values = { QString::fromLatin1(SaleState::Paid) };
    ReportFilter any = f;
    any.user.clear();
    QSqlQuery q(db());
    if (!run(q, QStringLiteral(
            "SELECT v.fecha, v.caja_id, v.numero, v.cliente_nombre, v.cliente_documento, v.gravado10, v.iva10, v.gravado5,"
            " v.iva5, v.exento, v.neto FROM ventas v WHERE v.estado = ?")
            + saleClause(QStringLiteral("v"), QStringLiteral("usuario"), any, &values) + QStringLiteral(" ORDER BY v.fecha, v.id"), values))
        return false;
    r->columns = { { tr("Fecha"), ReportCell::DateTime }, { tr("Número"), ReportCell::Text }, { tr("Cliente"), ReportCell::Text },
                   { tr("Documento"), ReportCell::Text }, { tr("Gravado 10 %"), ReportCell::Money }, { tr("IVA 10 %"), ReportCell::Money },
                   { tr("Gravado 5 %"), ReportCell::Money }, { tr("IVA 5 %"), ReportCell::Money }, { tr("Exento"), ReportCell::Money },
                   { tr("Total"), ReportCell::Money } };
    Row totals = { tr("Total"), QVariant(), QVariant(), QVariant(), Money(0), Money(0), Money(0), Money(0), Money(0), Money(0) };
    while (q.next()) {
        const QString customer = q.value(3).toString();
        Row row = { q.value(0).toDateTime(), AguaraSale::formatNumber(q.value(1).toLongLong(), q.value(2).toLongLong()),
                    customer.isEmpty() ? tr("Consumidor final") : customer, q.value(4) };
        for (int i = 5; i <= 10; ++i) {
            row << q.value(i).toLongLong();
            totals[i - 1] = totals.at(i - 1).toLongLong() + q.value(i).toLongLong();
        }
        r->rows << row;
    }
    r->totals = totals;
    r->note = tr("Borrador interno: los tickets no son comprobantes fiscales, y el formato de la SET se define con el contador (fase 8).");
    return true;
}

// --- audit -------------------------------------------------------------------

bool SqlReportService::auditLog(const ReportFilter &f, ReportResult *r)
{
    r->columns = { { tr("Fecha"), ReportCell::DateTime }, { tr("Usuario"), ReportCell::Text }, { tr("Autorizó"), ReportCell::Text },
                   { tr("Puesto"), ReportCell::Text }, { tr("Acción"), ReportCell::Text }, { tr("Sobre"), ReportCell::Text },
                   { tr("Antes"), ReportCell::Text }, { tr("Después"), ReportCell::Text } };
    if (!m_users)
        return true;
    AuditFilter filter;
    filter.from = f.from;
    filter.to = f.to;
    filter.text = f.text;
    filter.limit = 5000;
    for (const AuditEntry &e : m_users->auditEntries(filter)) {
        if (!f.user.isEmpty() && e.user != f.user)
            continue;
        r->rows << Row{ e.when, e.user, e.authorizedBy, e.station, e.action,
                        QStringLiteral("%1 %2").arg(e.entity, e.entityId).trimmed(), e.before, e.after };
    }
    if (r->rows.size() >= filter.limit)
        r->note = tr("Se muestran los últimos %1 registros: acote las fechas.").arg(filter.limit);
    return true;
}

// --- leaving the program -----------------------------------------------------

bool SqlReportService::exportCsv(const ReportResult &report, const QString &fileName)
{
    Telemetry::count(QStringLiteral("reporte.csv"));
    QSaveFile file(fileName);
    if (!file.open(QIODevice::WriteOnly) || file.write(AguaraReport::toCsv(report)) < 0 || !file.commit()) {
        m_lastError = tr("No se pudo guardar %1.").arg(fileName);
        return false;
    }
    return true;
}

void SqlReportService::print(const ReportResult &report, QPrinter *printer)
{
    // A report is wide: on its side, whatever the printer's default is
    Telemetry::count(QStringLiteral("reporte.imprimir"));
    printer->setPageOrientation(report.columns.size() > 5 ? QPageLayout::Landscape : QPageLayout::Portrait);
    ReportPainter::paint(report, printer);
}

bool SqlReportService::exportPdf(const ReportResult &report, const QString &fileName)
{
    Telemetry::count(QStringLiteral("reporte.pdf"));
    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(fileName);
    printer.setPageSize(QPageSize(QPageSize::A4));
    printer.setPageMargins(QMarginsF(12, 12, 12, 12), QPageLayout::Millimeter);
    printer.setPageOrientation(report.columns.size() > 5 ? QPageLayout::Landscape : QPageLayout::Portrait);
    if (!ReportPainter::paint(report, &printer) || printer.printerState() == QPrinter::Error || !QFile::exists(fileName)) {
        m_lastError = tr("No se pudo guardar %1.").arg(fileName);
        return false;
    }
    return true;
}
