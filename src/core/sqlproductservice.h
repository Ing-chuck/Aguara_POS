#ifndef SQLPRODUCTSERVICE_H
#define SQLPRODUCTSERVICE_H

#include "productservice.h"

#include <QObject>
#include <QPointer>
#include <QSqlQueryModel>

class QSqlDatabase;
class QSqlQuery;

/// Read-only product list handed to modules (see ProductService).
class ProductTableModel : public QSqlQueryModel
{
    Q_OBJECT
public:
    ProductTableModel(const QString &connectionName, QObject *parent);

    bool refresh();     ///< false if the query failed (see lastError())

    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &item, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

private:
    QString m_connectionName;
};

class ConnectionMonitor;
class UserService;

/// ProductService on the core's database (SQLite or PostgreSQL).
class SqlProductService : public QObject, public ProductService
{
    Q_OBJECT
    Q_INTERFACES(ProductService)

public:
    /// `monitor` provides the connection and must outlive the service.
    explicit SqlProductService(ConnectionMonitor *monitor, QObject *parent = nullptr);

    /// Where changes are recorded (may stay null: nothing is recorded).
    void setAuditLog(UserService *users) { m_audit = users; }

    QAbstractItemModel *createProductTableModel(QObject *parent) override;
    std::optional<Product> product(const QString &code) const override;
    bool saveProduct(const Product &product, const QString &previousCode = QString()) override;
    bool deactivateProduct(const QString &code) override;
    bool importProducts(const QList<Product> &products, int *saved = nullptr) override;

    QList<NamedItem> brands() const override;
    qint64 addBrand(const QString &name) override;
    bool removeBrand(qint64 id) override;

    QList<NamedItem> categories() const override;
    qint64 addCategory(const QString &name, qint64 parentId = 0) override;
    bool setCategoryParent(qint64 id, qint64 parentId) override;
    bool removeCategory(qint64 id) override;

    QList<VatCategory> vatCategories() const override;
    QList<StockUnit> units() const override;

    /// Stock or prices changed elsewhere (InventoryService): refresh the
    /// lists here and on the other tills.
    void refreshAfterChange() { changed(); }

    QString lastError() const override { return m_lastError; }

private:
    QSqlDatabase db() const;
    bool exec(QSqlQuery &q) const;
    bool saveOne(const Product &product, const QString &previousCode, const QString &stockReason);
    qint64 findOrCreate(const QString &table, const QString &nameColumn, const QString &name);
    QList<NamedItem> namedItems(const QString &table, const QString &nameColumn) const;
    bool deactivate(const QString &table, qint64 id);
    void refreshModels();
    void changed();
    void subscribe();
    bool begin();
    bool commit();
    void rollback();

    ConnectionMonitor *m_monitor;
    UserService *m_audit = nullptr;
    bool m_inTransaction = false;
    QString m_connectionName;
    bool m_notifications = false;   // PostgreSQL LISTEN/NOTIFY active
    mutable QString m_lastError;
    QList<QPointer<ProductTableModel>> m_models;
};

#endif // SQLPRODUCTSERVICE_H
