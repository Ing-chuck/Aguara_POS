#ifndef ERRORS_H
#define ERRORS_H

#include <QString>

class QSqlError;

/*
    What the user reads when the database fails: a short sentence in Spanish
    that says what happened and what to do. The driver's own text ("QPSQL:
    Unable to send query", "UNIQUE constraint failed: ...") goes to the log
    file only (see logging.h): logAndExplain() does both.
*/
namespace AguaraErrors {

/// Short Spanish explanation of a database error.
QString forUser(const QSqlError &error);

/// Log the technical details under `context` (e.g. "saving product 777")
/// and return forUser(error).
QString logAndExplain(const QSqlError &error, const QString &context);

/// The sentence to append when the details are only in the log.
QString seeLog();

} // namespace AguaraErrors

#endif // ERRORS_H
