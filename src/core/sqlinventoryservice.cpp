#include "sqlinventoryservice.h"
#include "connectionmonitor.h"
#include "errors.h"
#include "sqlproductservice.h"
#include "stockledger.h"

#include <barcode.h>
#include <userservice.h>

#include <QHash>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace {

/// old * (100 % + percent) to the guaraní, then up to a multiple of roundTo
Money changedPrice(Money price, int percent, Money roundTo)
{
    const qint64 scaled = price * (10000 + percent);
    Money result = (scaled + 5000) / 10000;
    if (roundTo > 0 && result % roundTo != 0)
        result += roundTo - result % roundTo;
    return result;
}

QString placeholders(int count)
{
    QStringList marks;
    for (int i = 0; i < count; ++i)
        marks << QStringLiteral("?");
    return marks.join(QStringLiteral(", "));
}

} // namespace

SqlInventoryService::SqlInventoryService(ConnectionMonitor *monitor, SqlProductService *products, QObject *parent)
    : QObject(parent), m_monitor(monitor), m_products(products), m_connectionName(monitor->connectionName())
{
}

QSqlDatabase SqlInventoryService::db() const
{
    return QSqlDatabase::database(m_connectionName, false);
}

bool SqlInventoryService::exec(QSqlQuery &q) const
{
    if (m_monitor->exec(q, m_inTransaction))
        return true;
    m_lastError = m_monitor->lastError();
    return false;
}

bool SqlInventoryService::begin()
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

bool SqlInventoryService::commit()
{
    m_inTransaction = false;
    QSqlDatabase database = db();
    if (database.commit())
        return true;
    m_lastError = AguaraErrors::logAndExplain(database.lastError(), QStringLiteral("COMMIT"));
    database.rollback();
    return false;
}

void SqlInventoryService::rollback()
{
    m_inTransaction = false;
    db().rollback();
}

void SqlInventoryService::audit(const QString &action, const QString &entityId, const QString &before, const QString &after)
{
    if (m_audit)
        m_audit->audit(action, action.section(QLatin1Char('.'), 0, 0), entityId, before, after);
}

// --- stock -------------------------------------------------------------------

QList<StockMovement> SqlInventoryService::movements(const QString &code, int limit) const
{
    QList<StockMovement> result;
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT id, fecha, producto_codigo, tipo, cantidad, saldo, costo_unitario, motivo,"
                             " documento, usuario FROM stock_movimientos WHERE producto_codigo = ?"
                             " ORDER BY id DESC LIMIT %1").arg(qMax(limit, 1)));
    q.addBindValue(code);
    if (!exec(q))
        return result;
    while (q.next()) {
        StockMovement m;
        m.id = q.value(0).toLongLong();
        m.when = q.value(1).toDateTime();
        m.code = q.value(2).toString();
        m.type = q.value(3).toString();
        m.quantity = q.value(4).toLongLong();
        m.balance = q.value(5).toLongLong();
        m.unitCost = q.value(6).toLongLong();
        m.reason = q.value(7).toString();
        m.document = q.value(8).toString();
        m.user = q.value(9).toString();
        result << m;
    }
    return result;
}

bool SqlInventoryService::adjustStock(const QString &code, Quantity delta, const QString &reason)
{
    if (reason.trimmed().isEmpty()) {
        m_lastError = tr("Indique el motivo del ajuste.");
        return false;
    }
    if (delta == 0) {
        m_lastError = tr("El ajuste no cambia el stock.");
        return false;
    }
    const std::optional<Product> product = m_products->product(code);
    if (!product) {
        m_lastError = tr("El artículo %1 no existe.").arg(code);
        return false;
    }

    if (!begin())
        return false;
    StockLedger ledger(m_monitor, m_connectionName, &m_lastError, m_audit);
    if (!ledger.move(code, StockMovementType::Adjustment, delta, product->cost, reason.trimmed())) {
        rollback();
        return false;
    }
    if (!commit())
        return false;
    audit(QStringLiteral("stock.ajuste"), code, AguaraQuantity::format(product->stock, product->unit.decimals),
          QStringLiteral("%1 (%2)").arg(AguaraQuantity::format(product->stock + delta, product->unit.decimals), reason.trimmed()));
    m_products->refreshAfterChange();
    return true;
}

QList<Product> SqlInventoryService::lowStock() const
{
    QList<Product> result;
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT codigo FROM productos WHERE es_activo = 1 AND stock_min > 0"
                             " AND stock <= stock_min ORDER BY descripcion"));
    if (!exec(q))
        return result;
    QStringList codes;
    while (q.next())
        codes << q.value(0).toString();
    for (const QString &code : qAsConst(codes)) {
        if (const auto p = m_products->product(code))
            result << *p;
    }
    return result;
}

// --- codes -------------------------------------------------------------------

ScanResult SqlInventoryService::resolveScan(const QString &input) const
{
    const QString text = input.trimmed();
    ScanResult result;
    const auto accept = [&result](const Product &p) {
        result.found = true;
        result.product = p;
        result.price = p.price;
    };

    std::optional<Product> direct = m_products->product(text);
    if (direct && direct->active) {
        accept(*direct);
        return result;
    }

    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT producto_codigo FROM codigos_barras WHERE codigo = ?"));
    q.addBindValue(text);
    if (exec(q) && q.next()) {
        const std::optional<Product> p = m_products->product(q.value(0).toString());
        if (p && p->active) {
            accept(*p);
            return result;
        }
    }

    const AguaraBarcode::ScaleCode scale = AguaraBarcode::parseScaleCode(text);
    if (scale.valid) {
        const std::optional<Product> p = m_products->product(scale.articleCode);
        if (p && p->active) {
            accept(*p);
            if (scale.grams > 0) {
                result.quantity = scale.grams;      // a gram is a thousandth of a kg
            } else if (p->price > 0) {
                result.price = scale.price;
                result.quantity = (scale.price * AguaraQuantity::One + p->price / 2) / p->price;
            }
        }
    }
    return result;
}

QStringList SqlInventoryService::barcodes(const QString &code) const
{
    QStringList result;
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT codigo FROM codigos_barras WHERE producto_codigo = ? ORDER BY codigo"));
    q.addBindValue(code);
    if (!exec(q))
        return result;
    while (q.next())
        result << q.value(0).toString();
    return result;
}

bool SqlInventoryService::addBarcode(const QString &code, const QString &barcode)
{
    const QString text = barcode.trimmed();
    if (text.isEmpty()) {
        m_lastError = tr("Falta el código de barras.");
        return false;
    }
    if (m_products->product(text)) {
        m_lastError = tr("El código %1 ya es el código principal de un artículo.").arg(text);
        return false;
    }
    QSqlQuery taken(db());
    taken.prepare(QStringLiteral("SELECT producto_codigo FROM codigos_barras WHERE codigo = ?"));
    taken.addBindValue(text);
    if (!exec(taken))
        return false;
    if (taken.next()) {
        m_lastError = tr("El código %1 ya pertenece al artículo %2.").arg(text, taken.value(0).toString());
        return false;
    }
    taken.finish();
    QSqlQuery q(db());
    q.prepare(QStringLiteral("INSERT INTO codigos_barras (codigo, producto_codigo) VALUES (?, ?)"));
    q.addBindValue(text);
    q.addBindValue(code);
    if (!exec(q))
        return false;
    audit(QStringLiteral("producto.codigo"), code, QString(), text);
    return true;
}

bool SqlInventoryService::removeBarcode(const QString &barcode)
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("DELETE FROM codigos_barras WHERE codigo = ?"));
    q.addBindValue(barcode);
    if (!exec(q))
        return false;
    audit(QStringLiteral("producto.codigo"), QString(), barcode, QString());
    return true;
}

// --- suppliers ---------------------------------------------------------------

QList<Supplier> SqlInventoryService::suppliers(bool includeInactive) const
{
    QList<Supplier> result;
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT id, nombre, direccion, telefono, email, contacto, web, otros_detalles, es_activo"
                             " FROM proveedores %1 ORDER BY lower(nombre)")
                  .arg(includeInactive ? QString() : QStringLiteral("WHERE es_activo = 1")));
    if (!exec(q))
        return result;
    while (q.next()) {
        Supplier s;
        s.id = q.value(0).toLongLong();
        s.name = q.value(1).toString();
        s.address = q.value(2).toString();
        s.phone = q.value(3).toString();
        s.email = q.value(4).toString();
        s.contact = q.value(5).toString();
        s.web = q.value(6).toString();
        s.notes = q.value(7).toString();
        s.active = q.value(8).toBool();
        result << s;
    }
    return result;
}

qint64 SqlInventoryService::saveSupplier(const Supplier &supplier)
{
    const QString name = supplier.name.trimmed();
    if (name.isEmpty()) {
        m_lastError = tr("Falta el nombre del proveedor.");
        return 0;
    }
    QSqlQuery q(db());
    if (supplier.id) {
        q.prepare(QStringLiteral("UPDATE proveedores SET nombre = ?, direccion = ?, telefono = ?, email = ?, contacto = ?,"
                                 " web = ?, otros_detalles = ?, es_activo = ? WHERE id = ?"));
    } else {
        q.prepare(QStringLiteral("INSERT INTO proveedores (nombre, direccion, telefono, email, contacto, web,"
                                 " otros_detalles, es_activo) VALUES (?, ?, ?, ?, ?, ?, ?, ?)"));
    }
    q.addBindValue(name);
    q.addBindValue(supplier.address);
    q.addBindValue(supplier.phone);
    q.addBindValue(supplier.email);
    q.addBindValue(supplier.contact);
    q.addBindValue(supplier.web);
    q.addBindValue(supplier.notes);
    q.addBindValue(supplier.active ? 1 : 0);
    if (supplier.id)
        q.addBindValue(supplier.id);
    if (!exec(q))
        return 0;
    const qint64 id = supplier.id ? supplier.id : q.lastInsertId().toLongLong();
    audit(supplier.id ? QStringLiteral("proveedor.modificar") : QStringLiteral("proveedor.crear"),
          QString::number(id), QString(), name);
    return id;
}

bool SqlInventoryService::deactivateSupplier(qint64 id)
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("UPDATE proveedores SET es_activo = 0 WHERE id = ?"));
    q.addBindValue(id);
    if (!exec(q))
        return false;
    audit(QStringLiteral("proveedor.baja"), QString::number(id), QString(), QString());
    return true;
}

// --- purchases -----------------------------------------------------------------

qint64 SqlInventoryService::savePurchase(const Purchase &purchase)
{
    if (purchase.supplierId == 0) {
        m_lastError = tr("Elija el proveedor.");
        return 0;
    }
    if (purchase.lines.isEmpty()) {
        m_lastError = tr("La compra no tiene artículos.");
        return 0;
    }
    const QString invoice = purchase.invoice.trimmed();
    if (!invoice.isEmpty()) {       // the same invoice twice would double the stock
        QSqlQuery twice(db());
        twice.prepare(QStringLiteral("SELECT id FROM compras WHERE proveedor_id = ? AND factura = ?"));
        twice.addBindValue(purchase.supplierId);
        twice.addBindValue(invoice);
        if (!exec(twice))
            return 0;
        if (twice.next()) {
            m_lastError = tr("La factura %1 de este proveedor ya está cargada.").arg(invoice);
            return 0;
        }
    }

    Money total = 0;
    QHash<QString, Product> articles;
    for (const PurchaseLine &line : purchase.lines) {
        const std::optional<Product> p = m_products->product(line.code);
        if (!p) {
            m_lastError = tr("El artículo %1 no existe.").arg(line.code);
            return 0;
        }
        if (line.quantity <= 0 || line.unitCost < 0 || line.newPrice < 0) {
            m_lastError = tr("Revise la cantidad y el costo de %1.").arg(p->description);
            return 0;
        }
        articles.insert(line.code, *p);
        total += AguaraQuantity::lineTotal(line.unitCost, line.quantity);
    }

    const QDate date = purchase.date.isValid() ? purchase.date : QDate::currentDate();
    const QString isoDate = date.toString(Qt::ISODate);
    const auto user = m_audit ? m_audit->currentUser() : std::nullopt;
    const QString document = invoice.isEmpty() ? tr("Compra sin factura") : tr("Factura %1").arg(invoice);

    if (!begin())
        return 0;
    QSqlQuery head(db());
    head.prepare(QStringLiteral("INSERT INTO compras (fecha, proveedor_id, factura, total, usuario) VALUES (?, ?, ?, ?, ?)"));
    head.addBindValue(isoDate);
    head.addBindValue(purchase.supplierId);
    head.addBindValue(invoice);
    head.addBindValue(total);
    head.addBindValue(user ? user->username : QString());
    if (!exec(head)) {
        rollback();
        return 0;
    }
    const qint64 id = head.lastInsertId().toLongLong();

    StockLedger ledger(m_monitor, m_connectionName, &m_lastError, m_audit);
    for (const PurchaseLine &line : purchase.lines) {
        QSqlQuery item(db());
        item.prepare(QStringLiteral("INSERT INTO compra_items (compra_id, producto_codigo, cantidad, costo_unitario, precio_nuevo)"
                                    " VALUES (?, ?, ?, ?, ?)"));
        item.addBindValue(id);
        item.addBindValue(line.code);
        item.addBindValue(line.quantity);
        item.addBindValue(line.unitCost);
        item.addBindValue(line.newPrice);
        bool ok = exec(item) && ledger.move(line.code, StockMovementType::Purchase, line.quantity, line.unitCost,
                                            QString(), document);

        // the same article can come twice on an invoice: read what's current
        Product &article = articles[line.code];
        const Money newCost = purchase.updateCosts ? line.unitCost : article.cost;
        const Money newPrice = line.newPrice > 0 ? line.newPrice : article.price;
        if (ok) {
            QSqlQuery update(db());
            update.prepare(QStringLiteral("UPDATE productos SET costo = ?, precio = ?, f_compra = ?, f_mod = ?,"
                                          " proveedor_id = ? WHERE codigo = ?"));
            update.addBindValue(newCost);
            update.addBindValue(newPrice);
            update.addBindValue(isoDate);
            update.addBindValue(QDate::currentDate().toString(Qt::ISODate));
            update.addBindValue(purchase.supplierId);
            update.addBindValue(line.code);
            ok = exec(update) && ledger.recordPrice(line.code, article.price, newPrice, article.cost, newCost, document);
        }
        if (!ok) {
            rollback();
            return 0;
        }
        article.cost = newCost;
        article.price = newPrice;
    }
    if (!commit())
        return 0;

    audit(QStringLiteral("compra.cargar"), QString::number(id), QString(),
          tr("%1, %2 líneas, Gs. %3").arg(document).arg(purchase.lines.size()).arg(AguaraMoney::format(total)));
    m_products->refreshAfterChange();
    return id;
}

QList<Purchase> SqlInventoryService::purchases(const QDate &from, const QDate &to) const
{
    QList<Purchase> result;
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT c.id, c.fecha, c.proveedor_id, p.nombre, c.factura, c.total, c.usuario"
                             " FROM compras c JOIN proveedores p ON p.id = c.proveedor_id"
                             " WHERE c.fecha >= ? AND c.fecha <= ? ORDER BY c.id DESC"));
    q.addBindValue(from.toString(Qt::ISODate));
    q.addBindValue(to.toString(Qt::ISODate));
    if (!exec(q))
        return result;
    while (q.next()) {
        Purchase p;
        p.id = q.value(0).toLongLong();
        p.date = q.value(1).toDate();
        p.supplierId = q.value(2).toLongLong();
        p.supplierName = q.value(3).toString();
        p.invoice = q.value(4).toString();
        p.total = q.value(5).toLongLong();
        p.user = q.value(6).toString();
        result << p;
    }
    return result;
}

std::optional<Purchase> SqlInventoryService::purchase(qint64 id) const
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT c.fecha, c.proveedor_id, p.nombre, c.factura, c.total, c.usuario"
                             " FROM compras c JOIN proveedores p ON p.id = c.proveedor_id WHERE c.id = ?"));
    q.addBindValue(id);
    if (!exec(q) || !q.next())
        return std::nullopt;
    Purchase p;
    p.id = id;
    p.date = q.value(0).toDate();
    p.supplierId = q.value(1).toLongLong();
    p.supplierName = q.value(2).toString();
    p.invoice = q.value(3).toString();
    p.total = q.value(4).toLongLong();
    p.user = q.value(5).toString();
    q.finish();

    QSqlQuery lines(db());
    lines.prepare(QStringLiteral("SELECT i.producto_codigo, a.descripcion, i.cantidad, i.costo_unitario, i.precio_nuevo"
                                 " FROM compra_items i JOIN productos a ON a.codigo = i.producto_codigo"
                                 " WHERE i.compra_id = ? ORDER BY i.id"));
    lines.addBindValue(id);
    if (!exec(lines))
        return std::nullopt;
    while (lines.next()) {
        PurchaseLine line;
        line.code = lines.value(0).toString();
        line.description = lines.value(1).toString();
        line.quantity = lines.value(2).toLongLong();
        line.unitCost = lines.value(3).toLongLong();
        line.newPrice = lines.value(4).toLongLong();
        p.lines << line;
    }
    return p;
}

// --- physical counts -----------------------------------------------------------

QList<qint64> SqlInventoryService::categoryAndChildren(qint64 id) const
{
    QList<qint64> result = { id };
    const QList<NamedItem> all = m_products->categories();
    for (int i = 0; i < result.size(); ++i) {       // grows as children are found
        for (const NamedItem &c : all) {
            if (c.parentId == result.at(i) && !result.contains(c.id))
                result << c.id;
        }
    }
    return result;
}

QList<CountLine> SqlInventoryService::countSheet(qint64 categoryId) const
{
    QList<CountLine> result;
    const QList<qint64> categories = categoryId ? categoryAndChildren(categoryId) : QList<qint64>();
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT p.codigo, p.descripcion, p.stock, COALESCE(u.decimales, 0), COALESCE(u.abreviatura, 'un')"
                             " FROM productos p LEFT JOIN unidades_basicas u ON u.id = p.unidad_id"
                             " WHERE p.es_activo = 1 %1 ORDER BY p.descripcion")
                  .arg(categoryId ? QStringLiteral("AND p.rubro_id IN (%1)").arg(placeholders(categories.size())) : QString()));
    for (qint64 category : categories)
        q.addBindValue(category);
    if (!exec(q))
        return result;
    while (q.next()) {
        CountLine line;
        line.code = q.value(0).toString();
        line.description = q.value(1).toString();
        line.expected = q.value(2).toLongLong();
        line.counted = line.expected;
        line.decimals = q.value(3).toInt();
        line.unit = q.value(4).toString();
        result << line;
    }
    return result;
}

bool SqlInventoryService::applyCount(const QList<CountLine> &lines, const QString &note, int *adjusted)
{
    if (adjusted)
        *adjusted = 0;
    const QString reason = note.trimmed().isEmpty() ? tr("Recuento físico") : note.trimmed();

    if (!begin())
        return false;
    StockLedger ledger(m_monitor, m_connectionName, &m_lastError, m_audit);
    int count = 0;
    for (const CountLine &line : lines) {
        if (!line.wasCounted || line.counted == line.expected)
            continue;
        if (line.counted < 0) {
            rollback();
            m_lastError = tr("La cantidad contada de %1 no puede ser negativa.").arg(line.description);
            return false;
        }
        const std::optional<Product> p = m_products->product(line.code);
        if (!p || !ledger.move(line.code, StockMovementType::Count, line.counted - line.expected, p->cost, reason)) {
            rollback();
            return false;
        }
        ++count;
    }
    if (!commit())
        return false;
    if (adjusted)
        *adjusted = count;
    audit(QStringLiteral("stock.recuento"), QString(), QString(), tr("%1: %2 ajustes").arg(reason).arg(count));
    if (count > 0)
        m_products->refreshAfterChange();
    return true;
}

// --- prices --------------------------------------------------------------------

QList<PriceChange> SqlInventoryService::priceHistory(const QString &code, int limit) const
{
    QList<PriceChange> result;
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT id, fecha, precio_anterior, precio_nuevo, costo_anterior, costo_nuevo, motivo, usuario"
                             " FROM precios_historial WHERE producto_codigo = ? ORDER BY id DESC LIMIT %1").arg(qMax(limit, 1)));
    q.addBindValue(code);
    if (!exec(q))
        return result;
    while (q.next()) {
        PriceChange c;
        c.id = q.value(0).toLongLong();
        c.when = q.value(1).toDateTime();
        c.code = code;
        c.oldPrice = q.value(2).toLongLong();
        c.newPrice = q.value(3).toLongLong();
        c.oldCost = q.value(4).toLongLong();
        c.newCost = q.value(5).toLongLong();
        c.reason = q.value(6).toString();
        c.user = q.value(7).toString();
        result << c;
    }
    return result;
}

QList<PricePreview> SqlInventoryService::previewPriceChange(const PriceFilter &filter, int percent, Money roundTo) const
{
    QString sql = QStringLiteral("SELECT codigo, descripcion, precio FROM productos WHERE es_activo = 1 AND precio > 0");
    QVariantList values;
    if (filter.categoryId) {
        const QList<qint64> categories = categoryAndChildren(filter.categoryId);
        sql += QStringLiteral(" AND rubro_id IN (%1)").arg(placeholders(categories.size()));
        for (qint64 c : categories)
            values << c;
    }
    if (filter.brandId) {
        sql += QStringLiteral(" AND marca_id = ?");
        values << filter.brandId;
    }
    if (filter.supplierId) {
        sql += QStringLiteral(" AND proveedor_id = ?");
        values << filter.supplierId;
    }
    sql += QStringLiteral(" ORDER BY descripcion");

    QList<PricePreview> result;
    QSqlQuery q(db());
    q.prepare(sql);
    for (const QVariant &v : qAsConst(values))
        q.addBindValue(v);
    if (!exec(q))
        return result;
    while (q.next()) {
        PricePreview p;
        p.code = q.value(0).toString();
        p.description = q.value(1).toString();
        p.oldPrice = q.value(2).toLongLong();
        p.newPrice = changedPrice(p.oldPrice, percent, roundTo);
        result << p;
    }
    return result;
}

bool SqlInventoryService::applyPriceChange(const PriceFilter &filter, int percent, Money roundTo, int *changed)
{
    if (changed)
        *changed = 0;
    const QList<PricePreview> list = previewPriceChange(filter, percent, roundTo);
    const QString reason = tr("Cambio masivo %1 %").arg(QString::number(percent / 100.0, 'f', 2));

    if (!begin())
        return false;
    StockLedger ledger(m_monitor, m_connectionName, &m_lastError, m_audit);
    int count = 0;
    for (const PricePreview &p : list) {
        if (p.newPrice == p.oldPrice)
            continue;
        const std::optional<Product> article = m_products->product(p.code);
        QSqlQuery update(db());
        update.prepare(QStringLiteral("UPDATE productos SET precio = ?, f_mod = ? WHERE codigo = ?"));
        update.addBindValue(p.newPrice);
        update.addBindValue(QDate::currentDate().toString(Qt::ISODate));
        update.addBindValue(p.code);
        if (!article || !exec(update)
            || !ledger.recordPrice(p.code, p.oldPrice, p.newPrice, article->cost, article->cost, reason)) {
            rollback();
            return false;
        }
        ++count;
    }
    if (!commit())
        return false;
    if (changed)
        *changed = count;
    audit(QStringLiteral("precio.masivo"), QString(), QString(), tr("%1: %2 artículos").arg(reason).arg(count));
    m_products->refreshAfterChange();
    return true;
}
