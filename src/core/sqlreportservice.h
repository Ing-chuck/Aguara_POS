#ifndef SQLREPORTSERVICE_H
#define SQLREPORTSERVICE_H

#include <money.h>
#include <quantity.h>
#include <reportservice.h>

#include <QObject>
#include <QSqlDatabase>

class ConnectionMonitor;
class QSqlQuery;
class SqlProductService;
class UserService;

/// ReportService on the core's database. Nothing is rounded twice or summed
/// by the database where a report must match another: what's grouped by day or
/// hour is grouped here, so both databases answer the same.
class SqlReportService : public QObject, public ReportService
{
    Q_OBJECT
    Q_INTERFACES(ReportService)

public:
    /// `monitor` and `products` must outlive the service.
    SqlReportService(ConnectionMonitor *monitor, SqlProductService *products, QObject *parent = nullptr);

    /// Needed by the audit report (may stay null: the report comes out empty).
    void setUsers(UserService *users) { m_users = users; }

    QList<ReportDefinition> reports() const override;
    std::optional<ReportResult> run(const QString &id, const ReportFilter &filter) override;
    void addProvider(ReportProvider *provider) override { m_providers << provider; }
    QList<ReportChoice> registers() const override;
    QList<ReportChoice> sellers() const override;
    QList<ReportChoice> categories() const override;
    bool exportCsv(const ReportResult &report, const QString &fileName) override;
    bool exportPdf(const ReportResult &report, const QString &fileName) override;
    void print(const ReportResult &report, QPrinter *printer) override;
    QString lastError() const override { return m_lastError; }

private:
    /// A line sold, or (negative) returned.
    struct Line {
        QString code, description, category, brand, user;
        Quantity quantity = 0;
        Money amount = 0;
        Money cost = 0;
    };
    using Builder = bool (SqlReportService::*)(const ReportFilter &, ReportResult *);
    struct Entry {
        ReportDefinition definition;
        Builder build;
    };
    static const QList<Entry> &entries();

    QSqlDatabase db() const;
    bool run(QSqlQuery &q, const QString &sql, const QVariantList &values) const;
    QVariant timeValue(const QDateTime &when) const;
    QString dateClause(const QString &column, const ReportFilter &f, QVariantList *values) const;
    QString saleClause(const QString &alias, const QString &userColumn, const ReportFilter &f, QVariantList *values) const;
    QString categoryClause(const ReportFilter &f) const;
    bool lines(const ReportFilter &f, QList<Line> *out);
    QString describe(const ReportFilter &f, int filters) const;

    bool salesByDay(const ReportFilter &f, ReportResult *r);
    bool salesByHour(const ReportFilter &f, ReportResult *r);
    bool salesByArticle(const ReportFilter &f, ReportResult *r);
    bool salesByCategory(const ReportFilter &f, ReportResult *r);
    bool salesByBrand(const ReportFilter &f, ReportResult *r);
    bool salesByCashier(const ReportFilter &f, ReportResult *r);
    bool salesByMethod(const ReportFilter &f, ReportResult *r);
    bool salesByPromotion(const ReportFilter &f, ReportResult *r);
    bool marginByArticle(const ReportFilter &f, ReportResult *r);
    bool marginByCategory(const ReportFilter &f, ReportResult *r);
    bool cashSessions(const ReportFilter &f, ReportResult *r);
    bool cashMovements(const ReportFilter &f, ReportResult *r);
    bool voidsAndReturns(const ReportFilter &f, ReportResult *r);
    bool stockValue(const ReportFilter &f, ReportResult *r);
    bool stockLow(const ReportFilter &f, ReportResult *r);
    bool stockIdle(const ReportFilter &f, ReportResult *r);
    bool kardex(const ReportFilter &f, ReportResult *r);
    bool purchasesBySupplier(const ReportFilter &f, ReportResult *r);
    bool adjustments(const ReportFilter &f, ReportResult *r);
    bool vatSummary(const ReportFilter &f, ReportResult *r);
    bool salesBook(const ReportFilter &f, ReportResult *r);
    bool auditLog(const ReportFilter &f, ReportResult *r);

    ConnectionMonitor *m_monitor;
    SqlProductService *m_products;
    QString m_connectionName;
    UserService *m_users = nullptr;
    QList<ReportProvider *> m_providers;
    mutable QString m_lastError;
};

#endif // SQLREPORTSERVICE_H
