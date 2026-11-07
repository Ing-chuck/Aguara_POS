#ifndef SQLPROMOTIONSERVICE_H
#define SQLPROMOTIONSERVICE_H

#include <promotionservice.h>

#include <QHash>
#include <QObject>
#include <QSqlDatabase>

class ConnectionMonitor;
class QSqlQuery;
class UserService;

/// PromotionService on the core's database.
class SqlPromotionService : public QObject, public PromotionService
{
    Q_OBJECT
    Q_INTERFACES(PromotionService)

public:
    explicit SqlPromotionService(ConnectionMonitor *monitor, QObject *parent = nullptr);

    void setUsers(UserService *users) { m_users = users; }

    QList<Promotion> promotions(bool inactive = false) const override;
    qint64 savePromotion(const Promotion &promotion) override;
    bool setPromotionActive(qint64 id, bool active) override;
    std::optional<QList<Promotion>> offersFor(const QString &code, const QDate &day) const override;
    QList<Product> articlesIn(const Promotion &promotion) const override;
    QString lastError() const override { return m_lastError; }

private:
    QSqlDatabase db() const;
    bool exec(QSqlQuery &q) const;
    bool valid(const Promotion &p) const;
    /// All the promotions, or only the active ones, with their rules. Without a
    /// value if they can't be read.
    std::optional<QList<Promotion>> read(bool inactive) const;
    /// Category -> the one above it.
    std::optional<QHash<qint64, qint64>> categoryParents() const;
    bool writeRules(qint64 id, const QList<PromotionRule> &rules);

    ConnectionMonitor *m_monitor;
    QString m_connectionName;
    UserService *m_users = nullptr;
    mutable QString m_lastError;
    bool m_inTransaction = false;
};

#endif // SQLPROMOTIONSERVICE_H
