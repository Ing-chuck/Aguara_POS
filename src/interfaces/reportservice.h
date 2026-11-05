#ifndef REPORTSERVICE_H
#define REPORTSERVICE_H

#include "apiversion.h"
#include "aguara_global.h"

#include <QByteArray>
#include <QDate>
#include <QList>
#include <QString>
#include <QVariant>
#include <QVector>
#include <QtPlugin>

#include <optional>

class QPrinter;

/// What a report lets the user choose.
namespace ReportFilters {
enum {
    Dates    = 1,
    Register = 2,
    User     = 4,
    Category = 8,
    Text     = 16,      ///< see ReportDefinition::textLabel
    Days     = 32
};
}

struct ReportFilter {
    QDate   from;               ///< invalid: no limit
    QDate   to;                 ///< inclusive
    qint64  registerId = 0;     ///< 0: all
    QString user;               ///< empty: all
    qint64  categoryId = 0;     ///< 0: all; includes its subcategories
    QString text;
    int     days = 30;
};

/// An entry of the list of reports.
struct ReportDefinition {
    QString id;
    QString title;
    QString group;              ///< heading: "Ventas", "Caja"...
    int     filters = 0;        ///< ReportFilters
    QString permission;         ///< besides opening Reportes; empty: none
    QString textLabel;          ///< what the Text filter asks for
};

/// How a column's values are shown.
enum class ReportCell {
    Text,
    Integer,
    Money,                      ///< Money
    Quantity,                   ///< Quantity: thousandths
    Percent,                    ///< double
    Date,
    DateTime
};

struct ReportColumn {
    QString    title;
    ReportCell kind = ReportCell::Text;
};

/// The same for every report: the screen, the printer, CSV and PDF read this.
struct ReportResult {
    QString title;
    QString subtitle;           ///< what was asked: dates, register...
    QList<ReportColumn> columns;
    QList<QVector<QVariant>> rows;
    QVector<QVariant> totals;   ///< empty: no totals line
    QString note;               ///< how to read it
};

/// A name to choose in a filter.
struct ReportChoice {
    qint64  id = 0;
    QString name;
};

namespace AguaraReport {

/// A value as the screen and the paper show it.
AGUARA_EXPORT QString formatCell(ReportCell kind, const QVariant &value);

/// For Excel in Spanish: UTF-8 with its mark, ";" between fields, amounts
/// without thousands separators. A text that Excel would run as a formula goes
/// with a leading quote.
AGUARA_EXPORT QByteArray toCsv(const ReportResult &report);

AGUARA_EXPORT QString toHtml(const ReportResult &report);

} // namespace AguaraReport

/// Capability: a module adds reports of its own. The core asks for them when it
/// loads the module, and they appear in the list among the others.
class ReportProvider {
public:
    virtual ~ReportProvider() = default;
    virtual QList<ReportDefinition> reports() const = 0;
    /// `id` is one of reports(). Sets `error` and returns false on failure.
    virtual bool runReport(const QString &id, const ReportFilter &filter, ReportResult *result, QString *error) = 0;
};

/// The reports. Provided by the core. Every method returns false or an empty
/// result on failure, with the reason in lastError().
class ReportService {
public:
    virtual ~ReportService() = default;

    /// All of them: who may see each one is ReportDefinition::permission.
    virtual QList<ReportDefinition> reports() const = 0;
    virtual std::optional<ReportResult> run(const QString &id, const ReportFilter &filter) = 0;

    virtual void addProvider(ReportProvider *provider) = 0;

    // What can be chosen in the filters
    virtual QList<ReportChoice> registers() const = 0;
    virtual QList<ReportChoice> sellers() const = 0;
    virtual QList<ReportChoice> categories() const = 0;

    virtual bool exportCsv(const ReportResult &report, const QString &fileName) = 0;
    virtual bool exportPdf(const ReportResult &report, const QString &fileName) = 0;
    virtual void print(const ReportResult &report, QPrinter *printer) = 0;

    virtual QString lastError() const = 0;
};

#define ReportProvider_iid "org.AguaraPOS.ReportProvider/" AGUARA_API_VERSION
Q_DECLARE_INTERFACE(ReportProvider, ReportProvider_iid)
#define ReportService_iid "org.AguaraPOS.ReportService/" AGUARA_API_VERSION
Q_DECLARE_INTERFACE(ReportService, ReportService_iid)

#endif // REPORTSERVICE_H
