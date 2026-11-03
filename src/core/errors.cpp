#include "errors.h"
#include "logging.h"

#include <QCoreApplication>
#include <QDir>
#include <QSqlError>

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("AguaraErrors", text);
}

bool has(const QString &text, const char *needle)
{
    return text.contains(QLatin1String(needle), Qt::CaseInsensitive);
}

} // namespace

namespace AguaraErrors {

QString seeLog()
{
    const QString file = AguaraLog::filePath();
    return file.isEmpty()
        ? tr("El detalle técnico quedó en el registro del programa.")
        : tr("El detalle técnico quedó en el registro: %1").arg(QDir::toNativeSeparators(file));
}

QString forUser(const QSqlError &error)
{
    // PostgreSQL SQLSTATE codes and SQLite messages, most specific first.
    const QString code = error.nativeErrorCode();
    const QString text = error.driverText() + QLatin1Char(' ') + error.databaseText();

    if (has(text, "password authentication failed") || has(text, "no password supplied"))
        return tr("Usuario o contraseña de la base de datos incorrectos.");
    if (has(text, "database \"") && has(text, "does not exist"))
        return tr("La base de datos indicada no existe en el servidor.");
    if (has(text, "no pg_hba.conf entry"))
        return tr("El servidor no permite conexiones desde este equipo (revise pg_hba.conf en el servidor).");
    if (error.type() == QSqlError::ConnectionError || has(text, "could not connect")
        || has(text, "server closed the connection") || has(text, "no connection to the server")
        || has(text, "connection pointer is NULL") || has(text, "timeout expired")
        || has(text, "Connection refused") || has(text, "could not translate host name"))
        return tr("No se puede conectar con el servidor de datos. Verifique que el servidor "
                  "esté encendido y que este equipo esté conectado a la red.");

    if (code == QLatin1String("23505") || has(text, "UNIQUE constraint failed"))
        return tr("Ya existe un registro con esos datos.");
    if (code == QLatin1String("23503") || has(text, "FOREIGN KEY constraint failed"))
        return tr("La operación no es posible: el registro está relacionado con otros datos.");
    if (code == QLatin1String("23514") || code == QLatin1String("23502")
        || has(text, "CHECK constraint failed") || has(text, "NOT NULL constraint failed"))
        return tr("Uno de los valores no es válido.");
    if (has(text, "database is locked") || has(text, "database table is locked")
        || code == QLatin1String("40P01") || code == QLatin1String("55P03"))
        return tr("Los datos están ocupados por otra operación. Intente de nuevo en unos segundos.");
    if (has(text, "readonly database") || has(text, "disk I/O error") || has(text, "database or disk is full")
        || has(text, "unable to open database file"))
        return tr("No se puede escribir el archivo de datos. Verifique el espacio libre en el disco "
                  "y los permisos de la carpeta.");
    if (has(text, "malformed") || has(text, "not a database"))
        return tr("El archivo de datos está dañado. Restaure la última copia de seguridad.");

    return tr("Error en la base de datos. %1").arg(seeLog());
}

QString logAndExplain(const QSqlError &error, const QString &context)
{
    qCWarning(lcDatabase).noquote() << context << "failed:" << error.text()
                                    << "| code" << error.nativeErrorCode() << "| type" << int(error.type());
    return forUser(error);
}

} // namespace AguaraErrors
