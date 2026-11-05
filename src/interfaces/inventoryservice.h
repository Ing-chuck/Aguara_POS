#ifndef INVENTORYSERVICE_H
#define INVENTORYSERVICE_H

#include "apiversion.h"
#include "money.h"
#include "productservice.h"
#include "quantity.h"

#include <QDate>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QtPlugin>

#include <optional>

/// Kinds of stock movement. Stored as text in stock_movimientos.tipo.
namespace StockMovementType {
constexpr const char *Initial    = "inicial";
constexpr const char *Purchase   = "compra";
constexpr const char *Sale       = "venta";
constexpr const char *Return     = "devolucion";
constexpr const char *Adjustment = "ajuste";
constexpr const char *Count      = "recuento";
}

/// One line of an article's kardex.
struct StockMovement {
    qint64    id = 0;
    QDateTime when;
    QString   code;
    QString   type;             ///< a StockMovementType
    Quantity  quantity = 0;     ///< positive in, negative out
    Quantity  balance = 0;      ///< stock after this movement
    Money     unitCost = 0;
    QString   reason;
    QString   document;         ///< e.g. "Factura 001-001-0000123"
    QString   user;
};

struct Supplier {
    qint64  id = 0;
    QString name;
    QString address;
    QString phone;
    QString email;
    QString contact;
    QString web;
    QString notes;
    bool    active = true;
};

struct PurchaseLine {
    QString  code;
    QString  description;       ///< filled in when reading
    Quantity quantity = 0;
    Money    unitCost = 0;
    Money    newPrice = 0;      ///< 0 keeps the sale price
};

/// Merchandise received from a supplier.
struct Purchase {
    qint64  id = 0;
    QDate   date;
    qint64  supplierId = 0;
    QString supplierName;       ///< filled in when reading
    QString invoice;
    QList<PurchaseLine> lines;
    bool    updateCosts = true; ///< the lines' costs become the articles' cost
    Money   total = 0;          ///< filled in when saved
    QString user;               ///< filled in when reading
};

/// One article of a physical count.
struct CountLine {
    QString  code;
    QString  description;
    int      decimals = 0;      ///< of its unit
    QString  unit;
    Quantity expected = 0;      ///< the stock when the sheet was made
    Quantity counted = 0;
    bool     wasCounted = false;///< false: not counted, no adjustment
};

struct PriceChange {
    qint64    id = 0;
    QDateTime when;
    QString   code;
    Money     oldPrice = 0;
    Money     newPrice = 0;
    Money     oldCost = 0;
    Money     newCost = 0;
    QString   reason;
    QString   user;
};

/// Which articles a bulk price change applies to (0: any). A category
/// includes the ones inside it.
struct PriceFilter {
    qint64 categoryId = 0;
    qint64 brandId = 0;
    qint64 supplierId = 0;
};

/// One article in the preview of a bulk price change.
struct PricePreview {
    QString code;
    QString description;
    Money   oldPrice = 0;
    Money   newPrice = 0;
};

/// What a code typed or scanned refers to.
struct ScanResult {
    bool     found = false;
    Product  product;
    Quantity quantity = AguaraQuantity::One;    ///< a scale's weight, otherwise 1
    Money    price = 0;                         ///< a scale's price, otherwise the article's
};

/// Stock, suppliers, purchases and prices. Provided by the core. Every change
/// of stock goes through here, and leaves a movement in the kardex.
class InventoryService {
public:
    virtual ~InventoryService() = default;

    // --- stock ----------------------------------------------------------
    /// Newest first.
    virtual QList<StockMovement> movements(const QString &code, int limit = 200) const = 0;
    /// Change the stock by `delta` (negative: out). The reason is required.
    /// The stock may go below zero: the sale of something not yet loaded
    /// must not stop a till.
    virtual bool adjustStock(const QString &code, Quantity delta, const QString &reason) = 0;
    /// Active articles at or below their minimum.
    virtual QList<Product> lowStock() const = 0;

    // --- codes ----------------------------------------------------------
    /// By main code, by alternate barcode, or as a scale's code.
    virtual ScanResult resolveScan(const QString &text) const = 0;
    virtual QStringList barcodes(const QString &code) const = 0;
    virtual bool addBarcode(const QString &code, const QString &barcode) = 0;
    virtual bool removeBarcode(const QString &barcode) = 0;

    // --- suppliers ------------------------------------------------------
    virtual QList<Supplier> suppliers(bool includeInactive = false) const = 0;
    /// Create (id == 0) or update; returns the id, 0 on error.
    virtual qint64 saveSupplier(const Supplier &supplier) = 0;
    virtual bool deactivateSupplier(qint64 id) = 0;

    // --- purchases ------------------------------------------------------
    /// All or nothing: the purchase, its stock movements and the new costs
    /// and prices. Returns the id, 0 on error.
    virtual qint64 savePurchase(const Purchase &purchase) = 0;
    /// Without their lines; newest first.
    virtual QList<Purchase> purchases(const QDate &from, const QDate &to) const = 0;
    virtual std::optional<Purchase> purchase(qint64 id) const = 0;

    // --- physical counts ------------------------------------------------
    /// The active articles of a category, and the ones inside it (0: all).
    virtual QList<CountLine> countSheet(qint64 categoryId) const = 0;
    /// One adjustment per counted line that differs. The difference is taken
    /// from the current stock, so sales made while counting aren't lost.
    /// `adjusted` receives the number of adjustments.
    virtual bool applyCount(const QList<CountLine> &lines, const QString &note, int *adjusted = nullptr) = 0;

    // --- prices ---------------------------------------------------------
    virtual QList<PriceChange> priceHistory(const QString &code, int limit = 100) const = 0;
    /// What a bulk change would do: `percent` in hundredths (1050 = +10,50 %),
    /// new prices rounded up to a multiple of `roundTo` Gs. (0: no rounding).
    virtual QList<PricePreview> previewPriceChange(const PriceFilter &filter, int percent, Money roundTo) const = 0;
    virtual bool applyPriceChange(const PriceFilter &filter, int percent, Money roundTo, int *changed = nullptr) = 0;

    virtual QString lastError() const = 0;
};

#define InventoryService_iid "org.AguaraPOS.InventoryService/" AGUARA_API_VERSION
Q_DECLARE_INTERFACE(InventoryService, InventoryService_iid)

#endif // INVENTORYSERVICE_H
