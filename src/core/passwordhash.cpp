#include "passwordhash.h"

#include <QCryptographicHash>
#include <QPasswordDigestor>
#include <QRandomGenerator>
#include <QStringList>

namespace {

// About 0.2 s on a current PC (32-bit release build), about a second on an old
// till: slow for someone guessing passwords, fine for signing in.
int g_defaultIterations = 120000;
const int kSaltBytes = 16;
const int kHashBytes = 32;
const char kScheme[] = "pbkdf2-sha256";

QByteArray derive(const QString &password, const QByteArray &salt, int iterations, int bytes)
{
    return QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha256, password.toUtf8(),
                                              salt, iterations, quint64(bytes));
}

} // namespace

namespace PasswordHash {

void setDefaultIterations(int iterations)
{
    g_defaultIterations = iterations;
}

QString create(const QString &password, int iterations)
{
    if (iterations <= 0)
        iterations = g_defaultIterations;
    QByteArray salt(kSaltBytes, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(salt.data()), kSaltBytes / 4);
    const QByteArray hash = derive(password, salt, iterations, kHashBytes);
    return QStringLiteral("%1$%2$%3$%4").arg(QLatin1String(kScheme)).arg(iterations)
        .arg(QString::fromLatin1(salt.toBase64()), QString::fromLatin1(hash.toBase64()));
}

bool verify(const QString &password, const QString &stored)
{
    const QStringList parts = stored.split(QLatin1Char('$'));
    if (parts.size() != 4 || parts.at(0) != QLatin1String(kScheme))
        return false;
    const int iterations = parts.at(1).toInt();
    if (iterations <= 0)
        return false;
    const QByteArray salt = QByteArray::fromBase64(parts.at(2).toLatin1());
    const QByteArray expected = QByteArray::fromBase64(parts.at(3).toLatin1());
    if (expected.isEmpty())
        return false;
    const QByteArray actual = derive(password, salt, iterations, expected.size());

    unsigned char difference = 0;
    for (int i = 0; i < expected.size(); ++i)
        difference |= static_cast<unsigned char>(expected.at(i) ^ actual.at(i));
    return difference == 0;
}

bool needsUpgrade(const QString &stored)
{
    return stored.section(QLatin1Char('$'), 1, 1).toInt() < g_defaultIterations;
}

} // namespace PasswordHash
