#ifndef SALEOUTBOX_H
#define SALEOUTBOX_H

#include <salesservice.h>

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>

#include <optional>

/// A charged sale on its way to the database.
struct OutboxEntry {
    qint64    id = 0;               ///< never reused
    QString   uuid;                 ///< identifies the sale: pushing it twice saves it once
    QDateTime when;
    QString   user;
    qint64    registerId = 0;
    qint64    sessionId = 0;
    qint64    number = 0;           ///< 0: the database gives it when it's pushed
    int       printCount = 0;
    SaleDraft draft;
    QList<SalePayment> payments;    ///< as received: checkout() works them out again
    QString   error;                ///< why the database refused it
};

/*
    Every sale, in a file of its own on this computer, from the moment it is
    charged until the database has it. The file is written to disk before the
    server is touched, so a power cut, a crash or a lost connection can only
    leave a sale here, never lose it.

    It is a local SQLite file, separate from the data: on the PostgreSQL setup
    it's the only thing that doesn't depend on the network.
*/
class SaleOutbox
{
public:
    explicit SaleOutbox(const QString &connectionName);
    ~SaleOutbox();
    SaleOutbox(const SaleOutbox &) = delete;
    SaleOutbox &operator=(const SaleOutbox &) = delete;

    /// Before the first use. Default: next to aguara.ini.
    void setPath(const QString &path) { m_path = path; }
    QString path() const;

    /// Returns false, with lastError(), if the file can't be used. Nothing
    /// can be sold then: a sale without its copy could be lost.
    bool open();
    bool isOpen() const { return m_open; }

    /// Saved to disk when it returns. Returns the id, or 0.
    qint64 add(const OutboxEntry &entry);
    bool setNumber(qint64 id, qint64 number);
    bool addPrint(qint64 id);
    bool setError(qint64 id, const QString &error);
    bool remove(qint64 id);

    /// Oldest first.
    QList<OutboxEntry> entries() const;
    std::optional<OutboxEntry> entry(qint64 id) const;
    int count() const;
    /// The highest number taken in this register, 0 if none.
    qint64 lastNumber(qint64 registerId) const;

    QString lastError() const { return m_lastError; }

private:
    bool run(const QString &sql, const QVariantList &values) const;
    QList<OutboxEntry> read(const QString &where, const QVariantList &values) const;

    QString m_connectionName;
    QString m_path;
    bool m_open = false;
    mutable QString m_lastError;
};

namespace SaleJson {
/// Money and quantities go as text: JSON numbers are doubles.
QByteArray fromDraft(const SaleDraft &draft);
SaleDraft toDraft(const QByteArray &json);
QByteArray fromPayments(const QList<SalePayment> &payments);
QList<SalePayment> toPayments(const QByteArray &json);
}

#endif // SALEOUTBOX_H
