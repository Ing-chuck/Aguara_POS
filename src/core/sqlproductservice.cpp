#include "sqlproductservice.h"
#include "errors.h"
#include "logging.h"
#include "connectionmonitor.h"

#include <aguarastyle.h>

#include <QDate>
#include <QFont>
#include <QDebug>
#include <QSqlDatabase>
#include <QSqlDriver>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
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
    "       p.iva, p.stock, p.stock_min, p.f_compra, p.f_mod"
    "  FROM productos p"
    "  LEFT JOIN marcas m ON m.id = p.marca_id"
    "  LEFT JOIN rubros r ON r.id = p.rubro_id"
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
        const int stock = QSqlQueryModel::data(index(item.row(), int(ProductColumn::Stock))).toInt();
        const int minimum = QSqlQueryModel::data(index(item.row(), int(ProductColumn::StockMin))).toInt();
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
        "       p.precio, p.costo, p.iva, p.stock, p.stock_min, p.f_compra, p.f_mod, p.es_activo"
        "  FROM productos p"
        "  LEFT JOIN marcas m ON m.id = p.marca_id"
        "  LEFT JOIN rubros r ON r.id = p.rubro_id"
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
    p.stock        = q.value(9).toInt();
    p.stockMin     = q.value(10).toInt();
    p.purchased    = QDate::fromString(q.value(11).toString(), Qt::ISODate);
    p.modified     = QDate::fromString(q.value(12).toString(), Qt::ISODate);
    p.active       = q.value(13).toBool();
    return p;
}

bool SqlProductService::saveProduct(const Product &product, const QString &previousCode)
{
    if (!begin())
        return false;
    if (!saveOne(product, previousCode)) {
        rollback();
        return false;
    }
    if (!commit())
        return false;
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
        if (!saveOne(p, QString())) {
            m_lastError = tr("Artículo %1: %2").arg(p.code, m_lastError);
            rollback();
            return false;
        }
    }
    if (!commit())
        return false;
    if (saved)
        *saved = products.size();
    changed();
    return true;
}

/// Insert or update one product. Must run inside a transaction.
bool SqlProductService::saveOne(const Product &in, const QString &previousCode)
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
    q.prepare(QStringLiteral("SELECT 1 FROM productos WHERE codigo = ?"));
    q.addBindValue(existingCode);
    if (!exec(q)) return false;
    const bool exists = q.next();

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
            "UPDATE productos SET codigo = ?, descripcion = ?, precio = ?, costo = ?, stock = ?,"
            " stock_min = ?, f_compra = ?, f_mod = ?, es_activo = 1, marca_id = ?, rubro_id = ?, iva = ?"
            " WHERE codigo = ?"));
    } else {
        w.prepare(QStringLiteral(
            "INSERT INTO productos (codigo, descripcion, precio, costo, stock, stock_min,"
            " f_compra, f_mod, es_activo, marca_id, rubro_id, iva)"
            " VALUES (?, ?, ?, ?, ?, ?, ?, ?, 1, ?, ?, ?)"));
    }
    w.addBindValue(p.code);
    w.addBindValue(p.description);
    w.addBindValue(p.price);
    w.addBindValue(p.cost);
    w.addBindValue(p.stock);
    w.addBindValue(p.stockMin);
    w.addBindValue(dateOrNull(p.purchased));
    w.addBindValue(QDate::currentDate().toString(Qt::ISODate));
    w.addBindValue(nullIfZero(p.brandId));
    w.addBindValue(nullIfZero(p.categoryId));
    w.addBindValue(p.vatRate);
    if (exists)
        w.addBindValue(existingCode);
    return exec(w);
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
        items.append({ q.value(0).toLongLong(), q.value(1).toString() });
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
    return namedItems(QStringLiteral("rubros"), QStringLiteral("rubro"));
}

qint64 SqlProductService::addCategory(const QString &name)
{
    return findOrCreate(QStringLiteral("rubros"), QStringLiteral("rubro"), name);
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
