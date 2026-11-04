#ifndef CUSTOMERSERVICE_H
#define CUSTOMERSERVICE_H

#include "aguara_global.h"

#include <QList>
#include <QString>
#include <QtPlugin>

#include <optional>

namespace DocumentType {
constexpr const char *None = "";
constexpr const char *Ruc  = "ruc";     ///< "80009735-1": the part after the hyphen is the check digit
constexpr const char *Ci   = "ci";      ///< identity card: digits only
}

struct Customer {
    qint64  id = 0;
    QString name;
    QString documentType;   ///< DocumentType
    QString document;
    QString address;
    QString phone;
    QString email;
    bool    active = true;
};

namespace AguaraCustomer {

/// The RUC's check digit: modulo 11, as the tax office (SET) defines it. A
/// letter in an old RUC counts as its ASCII code.
AGUARA_EXPORT int rucCheckDigit(const QString &base);
/// "80009735-1": base, hyphen, and the right check digit.
AGUARA_EXPORT bool validRuc(const QString &ruc);
AGUARA_EXPORT bool validCi(const QString &ci);

} // namespace AguaraCustomer

/// The customers. Provided by the core. A sale without a customer is for the
/// final consumer. Every method returns false, 0 or an empty result on
/// failure, with the reason in lastError().
class CustomerService {
public:
    virtual ~CustomerService() = default;

    /// By name or document, active ones unless `inactive`, by name.
    virtual QList<Customer> customers(const QString &filter = QString(), bool inactive = false) const = 0;
    virtual std::optional<Customer> customer(qint64 id) const = 0;

    /// New if id == 0. Checks the document. Returns the id. Check clientes.editar first.
    virtual qint64 saveCustomer(const Customer &customer) = 0;
    /// Customers are never deleted: past sales point to them.
    virtual bool setCustomerActive(qint64 id, bool active) = 0;

    virtual QString lastError() const = 0;
};

#define CustomerService_iid "org.AguaraPOS.CustomerService/1.0"
Q_DECLARE_INTERFACE(CustomerService, CustomerService_iid)

#endif // CUSTOMERSERVICE_H
