#ifndef PRODUCTSERVICE_H
#define PRODUCTSERVICE_H

#include "apiversion.h"
#include "money.h"
#include "quantity.h"

#include <QDate>
#include <QList>
#include <QString>
#include <QtPlugin>
#include <qnamespace.h>

#include <optional>

QT_BEGIN_NAMESPACE
class QAbstractItemModel;
class QObject;
QT_END_NAMESPACE

/// A brand or a category: id plus display name. Categories nest: `path` is
/// "ALMACEN > LACTEOS", and equals `name` for everything else.
struct NamedItem {
    qint64  id = 0;
    QString name;
    qint64  parentId = 0;
    QString path;
};

/// A unit of sale. `decimals` is 0 for what is counted, 3 for what is weighed.
struct StockUnit {
    qint64  id = 1;
    QString name;           ///< "Kilogramo"
    QString abbreviation;   ///< "kg"
    int     decimals = 0;
};

/// An IVA category: the rate is the key (10, 5, or 0 for "Exenta").
struct VatCategory {
    int     rate = 0;
    QString name;
};

/// One article for sale.
struct Product {
    QString code;           ///< barcode or internal code; text, so leading zeros survive
    QString description;

    /// Brand/category: by id, or by name (empty id = 0). When saving, a name
    /// without an id is looked up, and created if it doesn't exist yet.
    /// Both empty means "none".
    qint64  brandId = 0;
    QString brandName;
    qint64  categoryId = 0;
    QString categoryName;

    Money   price = 0;      ///< sale price, whole guaraníes, IVA included
    Money   cost  = 0;      ///< purchase cost, whole guaraníes
    int     vatRate = 10;   ///< one of vatCategories()
    /// Saving a different stock leaves a movement in the kardex. To change it
    /// on purpose use InventoryService::adjustStock(), which asks for a reason.
    Quantity stock = 0;
    Quantity stockMin = 0;
    StockUnit unit;
    QDate   purchased;      ///< date of last purchase
    QDate   modified;       ///< set by the service on every save
    bool    active = true;
};

/// Data role of the product list: true when the article's stock is at or
/// below its minimum (shown in red in the Stock column).
constexpr int ProductLowStockRole = Qt::UserRole + 1;

/// Column layout of the model returned by createProductTableModel().
enum class ProductColumn : int {
    Code = 0,
    Description,
    Brand,
    Category,
    Price,
    Cost,
    Vat,
    Stock,
    StockMin,
    Purchased,
    Modified,
    Count
};

/*
    Products, brands, categories and IVA categories.
    The core owns the data; modules read and change it only through here.
    Every method returns false / an empty result on failure, with the reason
    in lastError().
*/
class ProductService {
public:
    virtual ~ProductService() = default;

    // --- Products ---------------------------------------------------------

    /// Read-only table of active products, columns as in ProductColumn.
    /// The caller owns it (pass a parent). It refreshes itself whenever
    /// products change through this service.
    virtual QAbstractItemModel *createProductTableModel(QObject *parent) = 0;

    /// Look up one product by code, active or not.
    virtual std::optional<Product> product(const QString &code) const = 0;

    /// Insert or update. To change a product's code, pass the old one as
    /// previousCode. Saving an inactive product's code reactivates it.
    virtual bool saveProduct(const Product &product, const QString &previousCode = QString()) = 0;

    /// Hide a product from lists (it stays in the database, so past sales
    /// that refer to it remain valid).
    virtual bool deactivateProduct(const QString &code) = 0;

    /// Insert or update many products in one transaction: either all are
    /// saved or none. *saved receives the number saved.
    virtual bool importProducts(const QList<Product> &products, int *saved = nullptr) = 0;

    // --- Brands and categories ---------------------------------------------

    virtual QList<NamedItem> brands() const = 0;            ///< active, sorted by name
    virtual qint64 addBrand(const QString &name) = 0;       ///< returns the id (existing one if the name exists); 0 on error
    virtual bool removeBrand(qint64 id) = 0;                ///< hides it; products keep their brand

    virtual QList<NamedItem> categories() const = 0;
    /// Inside `parentId` (0: at the top). An existing name keeps its place.
    virtual qint64 addCategory(const QString &name, qint64 parentId = 0) = 0;
    /// Move a category. Refuses to put it inside itself.
    virtual bool setCategoryParent(qint64 id, qint64 parentId) = 0;
    virtual bool removeCategory(qint64 id) = 0;

    virtual QList<VatCategory> vatCategories() const = 0;   ///< highest rate first
    virtual QList<StockUnit> units() const = 0;             ///< "Unidad" first

    virtual QString lastError() const = 0;
};

#define ProductService_iid "org.AguaraPOS.ProductService/" AGUARA_API_VERSION
Q_DECLARE_INTERFACE(ProductService, ProductService_iid)

#endif // PRODUCTSERVICE_H
