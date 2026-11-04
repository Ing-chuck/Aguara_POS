#ifndef SALESSERVICE_H
#define SALESSERVICE_H

#include "aguara_global.h"
#include "money.h"
#include "quantity.h"

#include <QDateTime>
#include <QList>
#include <QString>
#include <QtPlugin>

#include <optional>

namespace PaymentMethod {
constexpr const char *Cash     = "efectivo";
// Charged outside the program, on a machine or in a bank it doesn't talk to:
// the code on the voucher is what proves the payment.
constexpr const char *Debit    = "tarjeta_debito";
constexpr const char *Credit   = "tarjeta_credito";
constexpr const char *Transfer = "transferencia";
constexpr const char *Qr       = "qr";
/// Store credit from a return (ReturnService): its code is the note's number.
constexpr const char *CreditNote = "nota_credito";
}

/// A way of paying besides cash, which is always there. All of them are
/// reconciled apart: only cash is counted in the drawer. The store adds its
/// own, and for those the code is the name.
struct PaymentMethodInfo {
    QString code;
    bool    needsCode = true;       ///< a voucher or operation number
    bool    active = true;
};

namespace SaleState {
constexpr const char *Paid   = "cobrada";
constexpr const char *Voided = "anulada";
}

/// A line of the sale being rung up.
struct SaleLine {
    QString  code;
    QString  description;
    QString  unit;                  ///< "un", "kg"
    int      decimals = 0;          ///< of the unit
    Quantity quantity = AguaraQuantity::One;
    Money    unitPrice = 0;         ///< IVA included
    /// A scale label with the price printed on it: that price is the line's
    /// amount, whatever quantity * unitPrice rounds to. 0: none.
    Money    fixedAmount = 0;
    Money    discount = 0;          ///< on this line
    int      vatRate = 10;
};

/// The sale being rung up: it lives in the screen until it is paid or held.
struct SaleDraft {
    QList<SaleLine> lines;
    Money totalDiscount = 0;        ///< on the whole sale
    qint64  customerId = 0;         ///< 0: the final consumer
    QString customerName;
    QString customerDocument;
};

/// A line as charged: `discount` includes its share of the sale's discount.
struct SaleItem {
    QString  code;
    QString  description;
    QString  unit;
    int      decimals = 0;
    Quantity quantity = 0;
    Money    unitPrice = 0;
    Money    gross = 0;             ///< before discounts
    Money    discount = 0;
    Money    net = 0;               ///< gross - discount
    int      vatRate = 10;
    Money    vat = 0;               ///< contained in `net`
};

/// What a sale adds up to. Exact: the items' `net` sum `net`. Before payment
/// there is no rounding and total == net: the customer owes the exact amount.
/// Rounding comes from paying in cash, and only on what is paid in cash (see
/// AguaraSale::cashRounding()); a saved sale has total = net + rounding.
struct SaleTotals {
    QList<SaleItem> items;
    Money gross = 0;
    Money discount = 0;
    Money net = 0;
    Money rounding = 0;             ///< from the cash paid: see cashRounding()
    Money total = 0;                ///< what the customer pays
    Money taxed10 = 0;              ///< amount at 10 %, IVA included
    Money vat10 = 0;
    Money taxed5 = 0;
    Money vat5 = 0;
    Money exempt = 0;
};

struct SalePayment {
    QString method;
    Money   amount = 0;             ///< what was handed over
    Money   change = 0;             ///< cash only
    QString reference;              ///< voucher or operation code: required unless in cash
};

/// A sale already saved.
struct SaleRecord {
    qint64     id = 0;
    qint64     registerId = 0;
    qint64     sessionId = 0;
    qint64     number = 0;
    QDateTime  when;
    QString    user;
    QString    state;               ///< SaleState
    SaleTotals totals;
    QList<SalePayment> payments;
    QString    voidedBy;
    QDateTime  voidedAt;
    QString    voidReason;
    int        printCount = 0;      ///< tickets so far: after the first, copies
    qint64     customerId = 0;      ///< 0: the final consumer
    QString    customerName;        ///< as it was on the day of the sale
    QString    customerDocument;
};

/// A sale charged on this computer that the database doesn't have yet.
struct PendingSale {
    qint64    registerId = 0;
    qint64    number = 0;           ///< 0 until it needs one of its own
    QDateTime when;
    QString   user;
    Money     total = 0;
    QString   error;                ///< why the database refused it; empty while it just waits
};

/// A sale set aside to serve another customer.
struct HeldSale {
    qint64    id = 0;
    QDateTime when;
    QString   user;
    SaleDraft draft;
};

namespace AguaraSale {

/// The smallest coin: an amount paid in cash can't be finer than this. Cards,
/// transfers and other electronic payments have no such limit: they pay the
/// exact amount, to the guaraní.
constexpr Money CashStep = 50;

/// What rounds an amount paid in cash to the nearest CashStep. Pass only the
/// part paid in cash: of 15.625 with 10.000 by card, the 5.625.
constexpr Money cashRounding(Money cashAmount)
{
    return (cashAmount + CashStep / 2) / CashStep * CashStep - cashAmount;
}

/// What is left to pay in cash when `electronic` of `net` went by card or
/// transfer. Cash is always worked out last. It can be 0 with a few guaraníes
/// left: no coin pays them, so they round away.
constexpr Money cashDue(Money net, Money electronic)
{
    return net - electronic + cashRounding(net - electronic);
}

/// Anything but cash: an exact amount, and no change.
AGUARA_EXPORT bool isElectronic(const QString &method);
/// "NC-000012", and back. 0 if it isn't one.
AGUARA_EXPORT QString formatCreditNote(qint64 id);
AGUARA_EXPORT qint64 creditNoteId(const QString &number);
/// "Tarjeta de débito", for the screens and the closing sheet.
AGUARA_EXPORT QString methodName(const QString &method);
/// As a code is kept and compared: trimmed, in capitals.
AGUARA_EXPORT QString normalizeReference(const QString &reference);

/// quantity * unitPrice, or the scale's price
AGUARA_EXPORT Money lineGross(const SaleLine &line);

/// Works a sale out: each line's share of the sale's discount (so the shares
/// add up exactly), IVA line by line with vatIncluded(), and totals by rate.
/// The result is exact: no rounding.
AGUARA_EXPORT SaleTotals totals(const SaleDraft &draft);

/// "001-000123": register and number, as printed.
AGUARA_EXPORT QString formatNumber(qint64 registerId, qint64 number);

} // namespace AguaraSale

/// Selling. Provided by the core. A sale needs an open session of this
/// register (see CashService). Every method returns false, 0 or an empty
/// result on failure, with the reason in lastError().
class SalesService {
public:
    virtual ~SalesService() = default;

    /// Charge a sale. Cards and transfers pay exact amounts, each with its code,
    /// and can't exceed the sale: they give no change. What they leave is paid
    /// in cash, the only part that rounds (see cashDue()). No payments at all
    /// is right only if nothing is owed. Everything is saved in one transaction:
    /// the sale, its lines, the payments, the stock movements and the number.
    /// Returns the sale's id, or 0 if it wasn't charged.
    ///
    /// The sale goes first to a queue on this computer's disk. Without a
    /// connection it waits there and the id is negative: sale() and
    /// recordPrint() take it, so its ticket prints as usual, with its number.
    /// It reaches the database by itself when the connection comes back. Only
    /// the sale already being rung up can be finished this way: see online().
    virtual qint64 checkout(const SaleDraft &draft, const QList<SalePayment> &payments) = 0;

    /// false: there's no connection with the server. Nothing new can start,
    /// and the screen must not let it: a sale that's on the screen is finished,
    /// and that's all.
    virtual bool online() const = 0;
    /// In the order to offer them. Without `inactive`, those in use. Without a
    /// connection, the last ones known. Credit notes aren't here.
    virtual QList<PaymentMethodInfo> paymentMethods(bool inactive = false) const = 0;
    /// New or changed. Check caja.medios_pago first.
    virtual bool savePaymentMethod(const PaymentMethodInfo &method) = 0;

    /// What the queue holds, oldest first.
    virtual QList<PendingSale> pendingSales() const = 0;
    /// Try to send the queue now. It also happens by itself. true if it's empty.
    virtual bool flushPending() = 0;

    /// The number of a sale, not voided, that already used this code. Empty if
    /// none. Terminals can repeat a number after closing a batch, so a repeat
    /// is not refused here: the screen asks for ventas.comprobante_repetido.
    virtual QString referenceUsedBy(const QString &reference) const = 0;

    virtual std::optional<SaleRecord> sale(qint64 id) const = 0;

    /// Newest first, without their items.
    virtual QList<SaleRecord> sessionSales(qint64 sessionId) const = 0;

    /// Void a sale of the current session: its stock comes back and its cash
    /// leaves the session's expected cash. A credit note gets its balance back;
    /// a card or a transfer is reversed outside the program. A sale with a
    /// return can't be voided. Check ventas.anular first.
    virtual bool voidSale(qint64 id, const QString &reason) = 0;

    /// One more ticket of this sale came out.
    virtual bool recordPrint(qint64 id) = 0;

    /// Set a sale aside, in the database: it survives a restart.
    virtual qint64 holdSale(const SaleDraft &draft) = 0;
    /// This register's held sales, oldest first.
    virtual QList<HeldSale> heldSales() const = 0;
    /// Take one back: it stops being held.
    virtual std::optional<SaleDraft> takeHeldSale(qint64 id) = 0;

    /// Why the last call failed, in Spanish for the user.
    virtual QString lastError() const = 0;
};

#define SalesService_iid "org.AguaraPOS.SalesService/1.2"
Q_DECLARE_INTERFACE(SalesService, SalesService_iid)

#endif // SALESSERVICE_H
