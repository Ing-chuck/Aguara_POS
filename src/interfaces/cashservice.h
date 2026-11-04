#ifndef CASHSERVICE_H
#define CASHSERVICE_H

#include "money.h"

#include <QDateTime>
#include <QList>
#include <QString>
#include <QVector>
#include <QtPlugin>

#include <optional>

namespace CashMovementType {
constexpr const char *In         = "ingreso";
constexpr const char *Out        = "egreso";       ///< payments to suppliers, expenses
constexpr const char *Withdrawal = "retiro";       ///< cash taken to the safe
}

/// How many of one note or coin.
struct DenominationCount {
    Money denomination = 0;
    int   count = 0;
};
using CashCount = QList<DenominationCount>;

namespace AguaraCash {

/// Notes and coins in circulation, largest first.
inline QVector<Money> denominations()
{
    return { 100000, 50000, 20000, 10000, 5000, 2000, 1000, 500, 100, 50 };
}

inline Money countTotal(const CashCount &count)
{
    Money total = 0;
    for (const DenominationCount &d : count)
        total += d.denomination * d.count;
    return total;
}

} // namespace AguaraCash

/// A till. Each computer is one register: it is created the first time the
/// computer needs it.
struct CashRegister {
    qint64  id = 0;
    QString name;
    QString station;
};

struct CashSession {
    qint64    id = 0;
    qint64    registerId = 0;
    QString   registerName;
    QDateTime opened;
    QString   openedBy;
    Money     openingFloat = 0;
    bool      open = true;
    QDateTime closed;
    QString   closedBy;
    Money     expected = 0;
    Money     counted = 0;
    Money     difference = 0;       ///< counted - expected
    QString   differenceReason;
    QString   authorizedBy;

    /// Forgotten from another day: only a supervisor may close it.
    bool isStale() const { return open && opened.date() < QDate::currentDate(); }
};

struct CashMovement {
    qint64    id = 0;
    QDateTime when;
    QString   type;                 ///< CashMovementType
    Money     amount = 0;           ///< always positive
    QString   reason;
    QString   user;
};

/// Money received with one payment method, net of change.
struct PaymentTotal {
    QString method;
    Money   amount = 0;
};

/// A card or transfer of the session, to check against the terminal's batch or
/// the bank's statement.
struct PaymentVoucher {
    QString saleNumber;
    QString method;
    QString reference;
    Money   amount = 0;
};

/// A return in this session.
struct SessionReturn {
    QString number;                 ///< "D-000012"
    QString saleNumber;
    QString method;
    QString reference;
    Money   amount = 0;             ///< what was given back
    QString reason;
    QString user;
    QString authorizedBy;
};

/// Everything the closing sheet shows.
struct CashSessionReport {
    CashSession session;
    int   salesCount = 0;
    Money salesTotal = 0;
    Money discounts = 0;
    Money rounding = 0;
    int   voidedCount = 0;
    Money voidedTotal = 0;
    Money taxed10 = 0;
    Money vat10 = 0;
    Money taxed5 = 0;
    Money vat5 = 0;
    Money exempt = 0;
    QList<PaymentTotal> payments;
    QList<PaymentVoucher> vouchers;
    QList<SessionReturn> returns;
    Money cashRefunds = 0;
    QList<CashMovement> movements;
    Money cashIn = 0;
    Money cashOut = 0;              ///< expenses and withdrawals
    Money expectedCash = 0;
    CashCount openingCount;
    CashCount closingCount;
};

enum class CloseResult {
    Closed,         ///< the count matched
    Mismatch,       ///< it didn't: count again, or a supervisor closes with the difference
    Failed          ///< see lastError()
};

/// Registers, sessions, cash movements and closing. Provided by the core.
/// The screens must not show the expected amount to whoever is counting: the
/// count at closing is blind.
class CashService {
public:
    virtual ~CashService() = default;

    /// This computer's register. Created the first time.
    virtual CashRegister currentRegister() = 0;
    virtual bool renameRegister(const QString &name) = 0;

    /// The open session of this register, if any, even from another day: an
    /// interrupted session is simply found again after a restart.
    virtual std::optional<CashSession> currentSession() = 0;

    /// `count` is the opening float. Returns false if a session is open.
    virtual bool openSession(const CashCount &count) = 0;

    /// CashMovementType, amount > 0, reason required. Check caja.movimientos first.
    virtual bool addMovement(const char *type, Money amount, const QString &reason) = 0;
    virtual QList<CashMovement> movements(qint64 sessionId) const = 0;

    /// Close if `count` matches the expected cash to the guaraní. A failed
    /// count is recorded in the audit log.
    virtual CloseResult closeSession(const CashCount &count) = 0;
    /// Close with a difference: after UserService::authorize() of
    /// caja.cerrar_diferencia. `reason` is required; the difference stays on
    /// the session and in the reports.
    virtual bool closeWithDifference(const CashCount &count, const QString &reason, const QString &authorizedBy) = 0;

    /// Newest first, of every register.
    virtual QList<CashSession> sessions(int limit = 200) const = 0;
    virtual std::optional<CashSessionReport> sessionReport(qint64 sessionId) const = 0;

    /// Why the last call failed, in Spanish for the user.
    virtual QString lastError() const = 0;
};

#define CashService_iid "org.AguaraPOS.CashService/1.0"
Q_DECLARE_INTERFACE(CashService, CashService_iid)

#endif // CASHSERVICE_H
