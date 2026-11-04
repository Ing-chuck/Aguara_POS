#ifndef PASSWORDHASH_H
#define PASSWORDHASH_H

#include <QString>

/*
    How passwords are stored: PBKDF2-SHA256 with a random salt per user.

        pbkdf2-sha256$<iterations>$<salt, base64>$<hash, base64>

    The iterations travel with the hash, so they can be raised in a later
    version without invalidating existing passwords (needsUpgrade()).
*/
namespace PasswordHash {

/// A new hash; `iterations` 0 uses the current default.
QString create(const QString &password, int iterations = 0);

/// Change the default. Only for tests, which hash hundreds of times.
void setDefaultIterations(int iterations);

/// Does `password` match `stored`? Compares in constant time.
bool verify(const QString &password, const QString &stored);

/// `stored` uses fewer iterations than the current default.
bool needsUpgrade(const QString &stored);

} // namespace PasswordHash

#endif // PASSWORDHASH_H
