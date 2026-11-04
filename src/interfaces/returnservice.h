#ifndef RETURNSERVICE_H
#define RETURNSERVICE_H

#include "cashservice.h"
#include "salesservice.h"

#include <QDateTime>
#include <QList>
#include <QString>
#include <QtPlugin>

#include <optional>

/// A line of a sale, and how much of it can still come back.
struct ReturnableItem {
    int      line = 0;              ///< 1: the first of the sale
    QString  code;
    QString  description;
    QString  unit;
    int      decimals = 0;
    Quantity sold = 0;
    Quantity returned = 0;          ///< before this return
    Money    net = 0;               ///< what the line was charged, discounts included
    Money    refunded = 0;          ///< of it, already given back
    int      vatRate = 10;
};

/// A sale, as a return sees it.
struct ReturnableSale {
    SaleRecord sale;
    QList<ReturnableItem> items;
    QList<PaymentTotal> refundable; ///< by method: what came in and hasn't gone back
};

struct ReturnLine {
    int      line = 0;
    Quantity quantity = 0;
};

struct ReturnRequest {
    qint64  saleId = 0;
    QList<ReturnLine> lines;
    QString method;                 ///< a method of the sale, or PaymentMethod::CreditNote
    QString reference;              ///< the card's reversal voucher, the transfer's number
    QString reason;                 ///< required
    QString authorizedBy;
};

struct ReturnedItem {
    QString  code;
    QString  description;
    QString  unit;
    int      decimals = 0;
    Quantity quantity = 0;
    Money    amount = 0;
    Money    vat = 0;
};

struct ReturnRecord {
    qint64    id = 0;
    QString   saleNumber;
    QDateTime when;
    QString   user;
    QString   authorizedBy;
    QString   reason;
    Money     amount = 0;           ///< exact
    Money     paid = 0;             ///< what was given back: in cash, it rounds
    QString   method;
    QString   reference;            ///< of a credit note: its number
    QString   customerName;
    QList<ReturnedItem> items;
};

/// Store credit, from a return. It's a payment method for a later sale until
/// its balance runs out. Internal: the fiscal credit note is phase 8.
struct CreditNote {
    qint64    id = 0;
    QString   number;               ///< "NC-000012"
    Money     amount = 0;
    Money     balance = 0;
    QDateTime when;
    QString   customerName;
};

namespace AguaraReturn {

/// How much `lines` give back, exact. Each line gives back its share of what
/// it was charged, and the last unit what's left, so that a sale returned in
/// parts adds up to the guaraní.
AGUARA_EXPORT Money amount(const ReturnableSale &sale, const QList<ReturnLine> &lines);

/// What a return of `amount` puts in the customer's hand.
AGUARA_EXPORT Money paid(const QString &method, Money amount);

/// "D-000012"
AGUARA_EXPORT QString formatNumber(qint64 id);

} // namespace AguaraReturn

/// Returns of past sales. Provided by the core. They need the server and an
/// open cash session: their money leaves that session. Every method returns
/// false, 0 or an empty result on failure, with the reason in lastError().
class ReturnService {
public:
    virtual ~ReturnService() = default;

    /// "001-000123", or "123" for this register.
    virtual std::optional<ReturnableSale> findSale(const QString &number) const = 0;

    /// In one transaction: the return, the stock and the credit note if any.
    /// Check ventas.devolver first. Returns its id.
    virtual qint64 processReturn(const ReturnRequest &request) = 0;
    virtual std::optional<ReturnRecord> returnRecord(qint64 id) const = 0;

    /// "NC-000012" or 12.
    virtual std::optional<CreditNote> creditNote(const QString &number) const = 0;

    virtual QString lastError() const = 0;
};

#define ReturnService_iid "org.AguaraPOS.ReturnService/1.0"
Q_DECLARE_INTERFACE(ReturnService, ReturnService_iid)

#endif // RETURNSERVICE_H
