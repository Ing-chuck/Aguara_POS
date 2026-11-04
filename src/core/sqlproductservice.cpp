#include "sqlproductservice.h"
#include "errors.h"
#include "logging.h"
#include "connectionmonitor.h"
#include "stockledger.h"

#include <aguarastyle.h>
#include <inventoryservice.h>
#include <userservice.h>

#include <QDate>
#include <QFont>
#include <QHash>
#include <QDebug>
#include <QSqlDatabase>
#include <QSqlDriver>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>

#include <algorithm>
#include <QVariant>

/*
    The SQL here runs unchanged on SQLite and PostgreSQL: standard SQL,
    '?' placeholders, lower() for case-insensitive names. Only the schema
    (database.cpp) differs per database.
*/

namespace {

// PostgreSQL LISTEN/NOTIFY channel: when one till changes products, the
// others refresh their lists.
const char *const kChangesChannel = "aguara_products";

const char *const kProductListSql =
    "SELECT p.codigo, p.descripcion, m.nombre, r.rubro, p.precio, p.costo,"
    "       p.iva, p.stock, p.stock_min, p.f_compra, p.f_mod,"
    "       COALESCE(u.decimales, 0), COALESCE(u.abreviatura, 'un')"    // after ProductColumn::Count
    "  FROM productos p"
    "  LEFT JOIN marcas m ON m.id = p.marca_id"
    "  LEFT JOIN rubros r ON r.id = p.rubro_id"
    "  LEFT JOIN unidades_basicas u ON u.id = p.unidad_id"
    " WHERE p.es_activo = 1"
    " ORDER BY p.codigo";

QVariant nullIfZero(qint64 id) { return id ? QVariant(id) : QVariant(QVariant::LongLong); }
QVariant dateOrNull(const QDate &d) { return d.isValid() ? QVariant(d.toString(Qt::ISODate)) : QVariant(QVariant::String); }

} // namespace

// --- ProductTableModel ------------------------------------------------------

ProductTableModel::ProductTableModel(const QString &connectionName, QObject *parent)
    : QSqlQueryModel(parent), m_connectionName(connectionName)
{
    refresh();
}

bool ProductTableModel::refresh()
{
    setQuery(QString::fromLatin1(kProductListSql), QSqlDatabase::database(m_connectionName, false));
    // SQLite results are fetched lazily; load everything so rowCount() is
    // the real count (a shop's catalog is thousands of rows, not millions).
    while (canFetchMore())
        fetchMore();
    if (lastError().isValid()) {
        qCWarning(lcDatabase) << "Product list refresh failed:" << lastError().text();
        return false;
    }
    return true;
}

int ProductTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(ProductColumn::Count);    // the unit is read through record()
}

QVariant ProductTableModel::data(const QModelIndex &item, int role) const
{
    const auto column = static_cast<ProductColumn>(item.column());
    const bool numeric = column == ProductColumn::Price || column == ProductColumn::Cost
                      || column == ProductColumn::Stock || column == ProductColumn::StockMin;

    if (role == Qt::TextAlignmentRole && numeric)
        return int(Qt::AlignRight | Qt::AlignVCenter);
    if (role == Qt::TextAlignmentRole && column == ProductColumn::Vat)
        return int(Qt::AlignCenter);

    // at or below the minimum: the stock shows in red, and pages can count them
    if (role == ProductLowStockRole || ((role == Qt::ForegroundRole || role == Qt::FontRole)
                                        && column == ProductColumn::Stock)) {
        const Quantity stock = QSqlQueryModel::data(index(item.row(), int(ProductColumn::Stock))).toLongLong();
        const Quantity minimum = QSqlQueryModel::data(index(item.row(), int(ProductColumn::StockMin))).toLongLong();
        const bool low = minimum > 0 && stock <= minimum;
        if (role == ProductLowStockRole)
            return low;
        if (!low)
            return QVariant();
        if (role == Qt::ForegroundRole)
            return AguaraStyle::color(AguaraStyle::Color::Danger);
        QFont bold;
        bold.setBold(true);
        return bold;
    }

    if (role != Qt::DisplayRole)
        return QSqlQueryModel::data(item, role);

    const QVariant raw = QSqlQueryModel::data(item, Qt::DisplayRole);
    switch (column) {
    case ProductColumn::Price:
    case ProductColumn::Cost:
        return AguaraMoney::format(raw.toLongLong());
    case ProductColumn::Vat:
        return raw.toInt() == 0 ? tr("Exenta") : QStringLiteral("%1%").arg(raw.toInt());
    case ProductColumn::Stock:
    case ProductColumn::StockMin: {
        const QSqlRecord row = record(item.row());
        const int decimals = row.value(int(ProductColumn::Count)).toInt();
        const QString text = AguaraQuantity::format(raw.toLongLong(), decimals);
        return decimals == 0 ? text : text + QLatin1Char(' ') + row.value(int(ProductColumn::Count) + 1).toString();
    }
    case ProductColumn::Purchased:
    case ProductColumn::Modified: {
        const QDate d = QDate::fromString(raw.toString(), Qt::ISODate);
        return d.isValid() ? d.toString(QStringLiteral("dd/MM/yyyy")) : QString();
    }
    default:
        return raw;
    }
}

QVariant ProductTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    const auto column = static_cast<ProductColumn>(section);
    if (orientation == Qt::Horizontal && role == Qt::TextAlignmentRole) {
        if (column == ProductColumn::Price || column == ProductColumn::Cost
            || column == ProductColumn::Stock || column == ProductColumn::StockMin)
            return int(Qt::AlignRight | Qt::AlignVCenter);
        if (column == ProductColumn::Vat)
            return int(Qt::AlignCenter);
        return int(Qt::AlignLeft | Qt::AlignVCenter);
    }
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return QSqlQueryModel::headerData(section, orientation, role);

    switch (column) {
    case ProductColumn::Code:        return tr("Código");
    case ProductColumn::Description: return tr("Descripción");
    case ProductColumn::Brand:       return tr("Marca");
    case ProductColumn::Category:    return tr("Rubro");
    case ProductColumn::Price:       return tr("Precio");
    case ProductColumn::Cost:        return tr("Costo");
    case ProductColumn::Vat:         return tr("IVA");
    case ProductColumn::Stock:       return tr("Stock");
    case ProductColumn::StockMin:    return tr("Mínimo");
    case ProductColumn::Purchased:   return tr("Compra");
    case ProductColumn::Modified:    return tr("Modificado");
    default:                         return QVariant();
    }
}

// --- SqlProductService ------------------------------------------------------

namespace {
/// What the audit log keeps of a product
QString auditText(const Product &p)
{
    return QStringLiteral("%1 | precio %2 | costo %3 | IVA %4 | stock %5 | minimo %6")
        .arg(p.description, AguaraMoney::format(p.price), AguaraMoney::format(p.cost))
        .arg(p.vatRate)
        .arg(AguaraQuantity::format(p.stock, p.unit.decimals), AguaraQuantity::format(p.stockMin, p.unit.decimals));
}
} // namespace

SqlProductService::SqlProductService(ConnectionMonitor *monitor, QObject *parent)
    : QObject(parent), m_monitor(monitor), m_connectionName(monitor->connectionName())
{
    // Shared database (PostgreSQL): hear about other tills' changes.
    // SQLite: only this program writes to the file, nothing to hear. (Qt's
    // SQLite driver also reports EventNotifications, for its own table
    // hooks, but has no NOTIFY: ask the monitor, not the driver.)
    QSqlDriver *driver = db().driver();
    if (m_monitor->isPostgres() && driver && driver->hasFeature(QSqlDriver::EventNotifications)) {
        m_notifications = true;
        subscribe();
        connect(driver, QOverload<const QString &, QSqlDriver::NotificationSource, const QVariant &>::of(&QSqlDriver::notification),
                this, [this](const QString &name, QSqlDriver::NotificationSource source, const QVariant &) {
                    if (name == QLatin1String(kChangesChannel) && source == QSqlDriver::OtherSource)
                        refreshModels();
                });
    }

    // After a lost connection is restored: LISTEN again (subscriptions belong
    // to the old connection) and reload, since other tills may have changed
    // products in the meantime.
    connect(m_monitor, &ConnectionMonitor::reconnected, this, [this]() {
        if (m_notifications)
            subscribe();
        refreshModels();
    });
}

void SqlProductService::subscribe()
{
    db().driver()->subscribeToNotification(QString::fromLatin1(kChangesChannel));
}

/// After a successful write: refresh this till's lists, and tell the others.
void SqlProductService::changed()
{
    refreshModels();
    if (m_notifications) {
        QSqlQuery q(db());
        // not prepared: PostgreSQL can't PREPARE a NOTIFY
        if (!m_monitor->exec(q, QStringLiteral("NOTIFY %1").arg(QString::fromLatin1(kChangesChannel))))
            qCWarning(lcDatabase) << "Could not notify other tills:" << m_monitor->lastError();
    }
}

QSqlDatabase SqlProductService::db() const
{
    return QSqlDatabase::database(m_connectionName, false);   // reopening is ConnectionMonitor's job
}

bool SqlProductService::exec(QSqlQuery &q) const
{
    // reconnects (and retries outside a transaction) if the server went away
    if (m_monitor->exec(q, m_inTransaction))
        return true;
    m_lastError = m_monitor->lastError();
    return false;
}

bool SqlProductService::begin()
{
    if (m_monitor->connectionBroken())
        m_monitor->reconnect();
    if (!db().transaction()) {
        // BEGIN is often what discovers a connection dropped while idle
        if (!(m_monitor->connectionBroken() && m_monitor->reconnect() && db().transaction())) {
            m_lastError = m_monitor->isConnected()
                ? AguaraErrors::logAndExplain(db().lastError(), QStringLiteral("BEGIN"))
                : tr("Sin conexión con el servidor de datos. Se reintenta automáticamente; "
                     "intente de nuevo en unos momentos.");
            return false;
        }
    }
    m_inTransaction = true;
    return true;
}

bool SqlProductService::commit()
{
    m_inTransaction = false;
    QSqlDatabase database = db();
    if (database.commit())
        return true;
    m_lastError = AguaraErrors::logAndExplain(database.lastError(), QStringLiteral("COMMIT"));
    database.rollback();
    return false;
}

void SqlProductService::rollback()
{
    m_inTransaction = false;
    db().rollback();
}

QAbstractItemModel *SqlProductService::createProductTableModel(QObject *parent)
{
    auto model = new ProductTableModel(m_connectionName, parent);
    m_models.append(model);
    if (model->lastError().isValid() && m_monitor->connectionBroken())
        m_monitor->reconnect();     // on success, reconnected() refreshes it
    return model;
}

void SqlProductService::refreshModels()
{
    bool failed = false;
    for (int i = m_models.size() - 1; i >= 0; --i) {
        if (m_models.at(i))
            failed = !m_models.at(i)->refresh() || failed;
        else
            m_models.removeAt(i);     // owner deleted it
    }
    if (failed && m_monitor->connectionBroken())
        m_monitor->reconnect();     // on success, reconnected() refreshes again
}

std::optional<Product> SqlProductService::product(const QString &code) const
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral(
        "SELECT p.codigo, p.descripcion, p.marca_id, m.nombre, p.rubro_id, r.rubro,"
        "       p.precio, p.costo, p.iva, p.stock, p.stock_min, p.f_compra, p.f_mod, p.es_activo,"
        "       COALESCE(u.id, 1), COALESCE(u.unidad, 'Unidad'), COALESCE(u.abreviatura, 'un'), COALESCE(u.decimales, 0)"
        "  FROM productos p"
        "  LEFT JOIN marcas m ON m.id = p.marca_id"
        "  LEFT JOIN rubros r ON r.id = p.rubro_id"
        "  LEFT JOIN unidades_basicas u ON u.id = p.unidad_id"
        " WHERE p.codigo = ?"));
    q.addBindValue(code);
    if (!exec(q) || !q.next())
        return std::nullopt;

    Product p;
    p.code         = q.value(0).toString();
    p.description  = q.value(1).toString();
    p.brandId      = q.value(2).toLongLong();
    p.brandName    = q.value(3).toString();
    p.categoryId   = q.value(4).toLongLong();
    p.categoryName = q.value(5).toString();
    p.price        = q.value(6).toLongLong();
    p.cost         = q.value(7).toLongLong();
    p.vatRate      = q.value(8).toInt();
    p.stock        = q.value(9).toLongLong();
    p.stockMin     = q.value(10).toLongLong();
    p.purchased    = QDate::fromString(q.value(11).toString(), Qt::ISODate);
    p.modified     = QDate::fromString(q.value(12).toString(), Qt::ISODate);
    p.active       = q.value(13).toBool();
    p.unit         = { q.value(14).toLongLong(), q.value(15).toString(), q.value(16).toString(), q.value(17).toInt() };
    return p;
}

bool SqlProductService::saveProduct(const Product &product, const QString &previousCode)
{
    const QString oldCode = previousCode.isEmpty() ? product.code.trimmed() : previousCode;
    const std::optional<Product> before = m_audit ? this->product(oldCode) : std::nullopt;

    if (!begin())
        return false;
    if (!saveOne(product, previousCode, tr("Corrección en la ficha del artículo"))) {
        rollback();
        return false;
    }
    if (!commit())
        return false;
    if (m_audit) {
        const std::optional<Product> after = this->product(product.code.trimmed());
        m_audit->audit(before ? QStringLiteral("producto.modificar") : QStringLiteral("producto.crear"),
                       QStringLiteral("producto"), product.code.trimmed(),
                       before ? auditText(*before) : QString(), after ? auditText(*after) : QString());
    }
    changed();
    return true;
}

bool SqlProductService::importProducts(const QList<Product> &products, int *saved)
{
    if (saved)
        *saved = 0;

    if (!begin())
        return false;
    for (const Product &p : products) {
        if (!saveOne(p, QString(), tr("Importación"))) {
            m_lastError = tr("Artículo %1: %2").arg(p.code, m_lastError);
            rollback();
            return false;
        }
    }
    if (!commit())
        return false;
    if (saved)
        *saved = products.size();
    if (m_audit)
        m_audit->audit(QStringLiteral("producto.importar"), QStringLiteral("producto"), QString(),
                       QString(), tr("%1 artículos").arg(products.size()));
    changed();
    return true;
}

/// Insert or update one product. Must run inside a transaction.
bool SqlProductService::saveOne(const Product &in, const QString &previousCode, const QString &stockReason)
{
    Product p = in;
    p.code = p.code.trimmed();
    p.description = p.description.trimmed();

    if (p.code.isEmpty())        { m_lastError = tr("Falta el código del artículo."); return false; }
    if (p.description.isEmpty()) { m_lastError = tr("Falta la descripción del artículo."); return false; }
    if (p.price < 0 || p.cost < 0) { m_lastError = tr("El precio y el costo no pueden ser negativos."); return false; }

    bool vatOk = false;
    for (const VatCategory &v : vatCategories())
        vatOk = vatOk || v.rate == p.vatRate;
    if (!vatOk) { m_lastError = tr("La tasa de IVA %1% no existe.").arg(p.vatRate); return false; }

    if (p.stockMin < 0) { m_lastError = tr("El stock mínimo no puede ser negativo."); return false; }

    if (!p.brandId && !p.brandName.trimmed().isEmpty()) {
        p.brandId = findOrCreate(QStringLiteral("marcas"), QStringLiteral("nombre"), p.brandName);
        if (!p.brandId) return false;
    }
    if (!p.categoryId && !p.categoryName.trimmed().isEmpty()) {
        p.categoryId = findOrCreate(QStringLiteral("rubros"), QStringLiteral("rubro"), p.categoryName);
        if (!p.categoryId) return false;
    }

    // Which row are we writing? (renaming = updating the row under its old code)
    const QString existingCode = previousCode.isEmpty() ? p.code : previousCode;
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT stock, precio, costo FROM productos WHERE codigo = ?"));
    q.addBindValue(existingCode);
    if (!exec(q)) return false;
    const bool exists = q.next();
    const Quantity oldStock = exists ? q.value(0).toLongLong() : 0;
    const Money oldPrice = exists ? q.value(1).toLongLong() : 0;
    const Money oldCost = exists ? q.value(2).toLongLong() : 0;
    q.finish();

    if (!exists || previousCode != p.code) {        // a code can't also be another article's barcode
        QSqlQuery alternate(db());
        alternate.prepare(QStringLiteral("SELECT producto_codigo FROM codigos_barras WHERE codigo = ?"));
        alternate.addBindValue(p.code);
        if (!exec(alternate)) return false;
        if (alternate.next() && alternate.value(0).toString() != existingCode) {
            m_lastError = tr("El código %1 ya es código de barras de %2.").arg(p.code, alternate.value(0).toString());
            return false;
        }
    }

    if (!previousCode.isEmpty() && previousCode != p.code) {
        if (!exists) { m_lastError = tr("El artículo %1 ya no existe.").arg(previousCode); return false; }
        QSqlQuery taken(db());
        taken.prepare(QStringLiteral("SELECT 1 FROM productos WHERE codigo = ?"));
        taken.addBindValue(p.code);
        if (!exec(taken)) return false;
        if (taken.next()) { m_lastError = tr("El código %1 ya lo usa otro artículo.").arg(p.code); return false; }
    }

    QSqlQuery w(db());
    if (exists) {
        w.prepare(QStringLiteral(
            "UPDATE productos SET codigo = ?, descripcion = ?, precio = ?, costo = ?,"
            " stock_min = ?, f_compra = ?, f_mod = ?, es_activo = 1, marca_id = ?, rubro_id = ?, iva = ?,"
            " unidad_id = ? WHERE codigo = ?"));
    } else {
        w.prepare(QStringLiteral(
            "INSERT INTO productos (codigo, descripcion, precio, costo, stock_min,"
            " f_compra, f_mod, es_activo, marca_id, rubro_id, iva, unidad_id)"
            " VALUES (?, ?, ?, ?, ?, ?, ?, 1, ?, ?, ?, ?)"));
    }
    w.addBindValue(p.code);
    w.addBindValue(p.description);
    w.addBindValue(p.price);
    w.addBindValue(p.cost);
    w.addBindValue(p.stockMin);
    w.addBindValue(dateOrNull(p.purchased));
    w.addBindValue(QDate::currentDate().toString(Qt::ISODate));
    w.addBindValue(nullIfZero(p.brandId));
    w.addBindValue(nullIfZero(p.categoryId));
    w.addBindValue(p.vatRate);
    w.addBindValue(p.unit.id > 0 ? p.unit.id : 1);
    if (exists)
        w.addBindValue(existingCode);
    if (!exec(w))
        return false;

    // the stock only changes through the ledger
    StockLedger ledger(m_monitor, m_connectionName, &m_lastError, m_audit);
    if (p.stock != oldStock
        && !ledger.move(p.code, exists ? StockMovementType::Adjustment : StockMovementType::Initial,
                        p.stock - oldStock, p.cost, exists ? stockReason : tr("Saldo inicial")))
        return false;
    return !exists || ledger.recordPrice(p.code, oldPrice, p.price, oldCost, p.cost, tr("Ficha del artículo"));
}

bool SqlProductService::deactivateProduct(const QString &code)
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("UPDATE productos SET es_activo = 0, f_mod = ? WHERE codigo = ?"));
    q.addBindValue(QDate::currentDate().toString(Qt::ISODate));
    q.addBindValue(code);
    if (!exec(q))
        return false;
    if (q.numRowsAffected() == 0) {
        m_lastError = tr("El artículo %1 no existe.").arg(code);
        return false;
    }
    if (m_audit) {
        const std::optional<Product> gone = product(code);
        m_audit->audit(QStringLiteral("producto.baja"), QStringLiteral("producto"), code,
                       gone ? auditText(*gone) : QString());
    }
    changed();
    return true;
}

/// Id of the active row named `name`; reactivates an inactive one, or inserts it.
qint64 SqlProductService::findOrCreate(const QString &table, const QString &nameColumn, const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        m_lastError = tr("Falta el nombre.");
        return 0;
    }

    // table/nameColumn are fixed by this class, never user input.
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT id, es_activo FROM %1 WHERE lower(%2) = lower(?)").arg(table, nameColumn));
    q.addBindValue(trimmed);
    if (!exec(q))
        return 0;

    if (q.next()) {
        const qint64 id = q.value(0).toLongLong();
        if (!q.value(1).toBool()) {
            QSqlQuery r(db());
            r.prepare(QStringLiteral("UPDATE %1 SET es_activo = 1 WHERE id = ?").arg(table));
            r.addBindValue(id);
            if (!exec(r))
                return 0;
        }
        return id;
    }

    QSqlQuery ins(db());
    ins.prepare(QStringLiteral("INSERT INTO %1 (%2) VALUES (?)").arg(table, nameColumn));
    ins.addBindValue(trimmed);
    if (!exec(ins))
        return 0;
    return ins.lastInsertId().toLongLong();
}

QList<NamedItem> SqlProductService::namedItems(const QString &table, const QString &nameColumn) const
{
    QList<NamedItem> items;
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT id, %2 FROM %1 WHERE es_activo = 1 ORDER BY lower(%2)").arg(table, nameColumn));
    if (!exec(q))
        return items;
    while (q.next())
        items.append({ q.value(0).toLongLong(), q.value(1).toString(), 0, q.value(1).toString() });
    return items;
}

bool SqlProductService::deactivate(const QString &table, qint64 id)
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("UPDATE %1 SET es_activo = 0 WHERE id = ?").arg(table));
    q.addBindValue(id);
    if (!exec(q))
        return false;
    changed();
    return true;
}

QList<NamedItem> SqlProductService::brands() const
{
    return namedItems(QStringLiteral("marcas"), QStringLiteral("nombre"));
}

qint64 SqlProductService::addBrand(const QString &name)
{
    return findOrCreate(QStringLiteral("marcas"), QStringLiteral("nombre"), name);
}

bool SqlProductService::removeBrand(qint64 id)
{
    return deactivate(QStringLiteral("marcas"), id);
}

QList<NamedItem> SqlProductService::categories() const
{
    QList<NamedItem> items;
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT id, rubro, parent_id FROM rubros WHERE es_activo = 1"));
    if (!exec(q))
        return items;
    QHash<qint64, NamedItem> byId;
    while (q.next()) {
        NamedItem item;
        item.id = q.value(0).toLongLong();
        item.name = q.value(1).toString();
        item.parentId = q.value(2).toLongLong();
        byId.insert(item.id, item);
    }
    for (NamedItem item : qAsConst(byId)) {
        QStringList path = { item.name };
        int depth = 0;      // a guard: the data can't loop, setCategoryParent refuses it
        for (qint64 up = item.parentId; byId.contains(up) && depth < 32; up = byId.value(up).parentId, ++depth)
            path.prepend(byId.value(up).name);
        item.path = path.join(QStringLiteral(" > "));
        items << item;
    }
    std::sort(items.begin(), items.end(), [](const NamedItem &a, const NamedItem &b) {
        return a.path.compare(b.path, Qt::CaseInsensitive) < 0;
    });
    return items;
}

bool SqlProductService::setCategoryParent(qint64 id, qint64 parentId)
{
    const QList<NamedItem> all = categories();
    for (qint64 up = parentId; up != 0;) {
        if (up == id) {
            m_lastError = tr("Un rubro no puede quedar dentro de sí mismo.");
            return false;
        }
        const auto found = std::find_if(all.cbegin(), all.cend(), [up](const NamedItem &c) { return c.id == up; });
        up = found != all.cend() ? found->parentId : 0;
    }
    QSqlQuery q(db());
    q.prepare(QStringLiteral("UPDATE rubros SET parent_id = ? WHERE id = ?"));
    q.addBindValue(nullIfZero(parentId));
    q.addBindValue(id);
    if (!exec(q))
        return false;
    changed();
    return true;
}

QList<StockUnit> SqlProductService::units() const
{
    QList<StockUnit> list;
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT id, unidad, abreviatura, decimales FROM unidades_basicas ORDER BY id"));
    if (!exec(q))
        return list;
    while (q.next())
        list.append({ q.value(0).toLongLong(), q.value(1).toString(), q.value(2).toString(), q.value(3).toInt() });
    return list;
}

qint64 SqlProductService::addCategory(const QString &name, qint64 parentId)
{
    const QList<NamedItem> all = categories();
    const bool existed = std::any_of(all.cbegin(), all.cend(), [&name](const NamedItem &c) {
        return c.name.compare(name.trimmed(), Qt::CaseInsensitive) == 0;
    });
    const qint64 id = findOrCreate(QStringLiteral("rubros"), QStringLiteral("rubro"), name);
    if (id && !existed && parentId)
        setCategoryParent(id, parentId);
    return id;
}

bool SqlProductService::removeCategory(qint64 id)
{
    return deactivate(QStringLiteral("rubros"), id);
}

QList<VatCategory> SqlProductService::vatCategories() const
{
    QList<VatCategory> list;
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT tasa, categoria FROM iva_cat ORDER BY tasa DESC"));
    if (!exec(q))
        return list;
    while (q.next())
        list.append({ q.value(0).toInt(), q.value(1).toString() });
    return list;
}
