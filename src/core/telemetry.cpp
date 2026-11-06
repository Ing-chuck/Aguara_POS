#include "telemetry.h"
#include "activitylog.h"
#include "database.h"
#include "logging.h"

#include <apiversion.h>
#include <version.h>

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>
#include <QUuid>

namespace {

const int kFirstSendMs = 60 * 1000;         // after the start: the till is busy with other things
const int kSendEveryMs = 60 * 60 * 1000;
const int kSaveEveryMs = 5 * 60 * 1000;
const int kTimeoutMs = 15 * 1000;
const int kResentDays = 14;                 // today's total grows: recent days go again
const int kKeptDays = 60;                   // and without a server, this much waits
const int kMostPerReport = 20;
const int kMostWaiting = 200;
const int kCrashesKept = 50;                // sent or not: they stay here for whoever looks into it

Telemetry *g_current = nullptr;

const char *const kUsageFile = "usage.json";

/// A plain identifier, as the server demands: no text gets through as an event.
QString eventName(const QString &event)
{
    static const QRegularExpression plain(QStringLiteral("^[a-z0-9][a-z0-9_.-]{0,79}$"));
    const QString name = event.toLower();
    return plain.match(name).hasMatch() ? name : QString();
}

QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
}

QString version()
{
    return QString::fromLatin1(VERSION_STRING).mid(1);      // without its "v"
}

QString endpointOf(const QString &endpoint, const char *path)
{
    QString url = endpoint.trimmed();
    while (url.endsWith(QLatin1Char('/')))
        url.chop(1);
    return url + QLatin1String(path);
}

QNetworkRequest requestFor(const TelemetrySettings &settings, const char *path)
{
    QNetworkRequest request{ QUrl(endpointOf(settings.endpoint, path)) };
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", "Bearer " + settings.key.toUtf8());
    request.setTransferTimeout(kTimeoutMs);
    return request;
}

/// The oldest first: if some must go, these are the ones that waited the most.
QStringList waiting(const QString &directory, const QString &pattern)
{
    QStringList files;
    const QFileInfoList found = QDir(directory).entryInfoList({ pattern }, QDir::Files, QDir::Time | QDir::Reversed);
    for (const QFileInfo &file : found)
        files << file.absoluteFilePath();
    return files;
}

QString idOf(const QString &file, const QString &prefix)
{
    return QFileInfo(file).completeBaseName().mid(prefix.size());
}

} // namespace

// --- settings ------------------------------------------------------------------

TelemetrySettings TelemetrySettings::load()
{
    TelemetrySettings s;
    QSettings ini(DatabaseSettings::settingsPath(), QSettings::IniFormat);
    ini.beginGroup(QStringLiteral("telemetry"));
    s.enabled = ini.value(QStringLiteral("enabled"), false).toBool();
    s.endpoint = ini.value(QStringLiteral("endpoint")).toString();
    s.installName = ini.value(QStringLiteral("install_name")).toString();
    s.installId = ini.value(QStringLiteral("install_id")).toString();
    AguaraSecret::unprotect(ini.value(QStringLiteral("key_protected")).toByteArray(), &s.key);
    return s;
}

void TelemetrySettings::save() const
{
    QDir().mkpath(QFileInfo(DatabaseSettings::settingsPath()).absolutePath());
    QSettings ini(DatabaseSettings::settingsPath(), QSettings::IniFormat);
    ini.beginGroup(QStringLiteral("telemetry"));
    ini.setValue(QStringLiteral("enabled"), enabled);
    ini.setValue(QStringLiteral("endpoint"), endpoint);
    ini.setValue(QStringLiteral("install_name"), installName);
    ini.setValue(QStringLiteral("install_id"), installId);
    ini.setValue(QStringLiteral("key_protected"), AguaraSecret::protect(key));
}

QString TelemetrySettings::problem() const
{
    const QUrl url(endpoint.trimmed(), QUrl::StrictMode);
    if (endpoint.trimmed().isEmpty())
        return QCoreApplication::translate("Telemetry", "Escriba la dirección del servidor.");
    if (!url.isValid() || url.host().isEmpty()
        || (url.scheme() != QLatin1String("http") && url.scheme() != QLatin1String("https")))
        return QCoreApplication::translate("Telemetry", "La dirección debe ser como https://servidor:8787.");
    if (key.trimmed().isEmpty())
        return QCoreApplication::translate("Telemetry", "Escriba la clave de envío.");
    return QString();
}

// --- Telemetry -----------------------------------------------------------------

Telemetry::Telemetry(const QString &directory, const QString &crashes, QObject *parent)
    : QObject(parent), directory(directory), crashDirectory(crashes.isEmpty() ? directory : crashes),
      network(new QNetworkAccessManager(this)),
      sender(new QTimer(this)), saver(new QTimer(this))
{
    g_current = this;
    load();
    sender->setSingleShot(true);
    connect(sender, &QTimer::timeout, this, [this]() {
        send();
        sender->start(kSendEveryMs);
    });
    saver->setInterval(kSaveEveryMs);
    connect(saver, &QTimer::timeout, this, &Telemetry::save);
}

Telemetry::~Telemetry()
{
    save();
    if (g_current == this)
        g_current = nullptr;
}

QString Telemetry::defaultDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/telemetry");
}

void Telemetry::count(const QString &event)
{
    const QString name = eventName(event);
    if (name.isEmpty())
        return;
    ActivityLog::record(name);
    if (g_current && g_current->current.enabled)
        g_current->addEvent(name);
}

void Telemetry::addEvent(const QString &name)
{
    days[QDate::currentDate().toString(Qt::ISODate)][name] += 1;
}

void Telemetry::configure(const TelemetrySettings &settings)
{
    current = settings;
    if (current.installId.isEmpty())
        current.installId = newId();
    if (current.enabled) {
        QDir().mkpath(directory);
        saver->start();
        if (!sender->isActive())
            sender->start(kFirstSendMs);
    } else {
        saver->stop();
        sender->stop();
    }
}

void Telemetry::setInstall(const QString &backendName, const QStringList &moduleIds)
{
    backend = backendName;
    modules.clear();
    for (const QString &id : moduleIds) {
        const QString name = eventName(id);
        if (!name.isEmpty())
            modules << name;
    }
}

void Telemetry::reportBug(const QString &text, const QString &contact)
{
    if (!current.enabled)
        return;
    QJsonObject bug;
    bug[QStringLiteral("when")] = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ssZ"));
    bug[QStringLiteral("version")] = version();
    bug[QStringLiteral("text")] = text.trimmed().left(4000);
    bug[QStringLiteral("contact")] = contact.trimmed().left(120);
    QDir().mkpath(directory);
    QSaveFile file(QDir(directory).filePath(QStringLiteral("bug-%1.json").arg(newId())));
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(bug).toJson(QJsonDocument::Compact)) < 0 || !file.commit())
        qCWarning(lcApp) << "Could not keep a bug report:" << file.errorString();
}

void Telemetry::load()
{
    QFile file(QDir(directory).filePath(QLatin1String(kUsageFile)));
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject all = QJsonDocument::fromJson(file.readAll()).object();
    for (auto day = all.constBegin(); day != all.constEnd(); ++day) {
        const QJsonObject events = day.value().toObject();
        for (auto event = events.constBegin(); event != events.constEnd(); ++event) {
            const QString name = eventName(event.key());
            if (!name.isEmpty())
                days[day.key()][name] = event.value().toInt();
        }
    }
    forgetOldDays(kKeptDays);
}

void Telemetry::save() const
{
    if (days.isEmpty())
        return;
    QJsonObject all;
    for (auto day = days.cbegin(); day != days.cend(); ++day) {
        QJsonObject events;
        for (auto event = day.value().cbegin(); event != day.value().cend(); ++event)
            events[event.key()] = event.value();
        all[day.key()] = events;
    }
    QDir().mkpath(directory);
    QSaveFile file(QDir(directory).filePath(QLatin1String(kUsageFile)));
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(all).toJson(QJsonDocument::Compact));
        file.commit();
    }
}

void Telemetry::forgetOldDays(int keep)
{
    const QString limit = QDate::currentDate().addDays(-keep).toString(Qt::ISODate);
    for (auto day = days.begin(); day != days.end();) {
        if (day.key() < limit)
            day = days.erase(day);
        else
            ++day;
    }
}

/// The only place that decides what leaves this computer.
QByteArray Telemetry::build(QStringList *files) const
{
    QJsonObject install;
    install[QStringLiteral("id")] = current.installId;
    install[QStringLiteral("name")] = current.installName.simplified().left(80);
    install[QStringLiteral("version")] = version();
    install[QStringLiteral("api")] = QString::fromLatin1(AGUARA_API_VERSION).toInt();
    install[QStringLiteral("os")] = QSysInfo::prettyProductName().left(80);
    install[QStringLiteral("backend")] = backend;
    install[QStringLiteral("modules")] = QJsonArray::fromStringList(modules);

    QJsonArray usage;
    for (auto day = days.cbegin(); day != days.cend(); ++day) {
        for (auto event = day.value().cbegin(); event != day.value().cend(); ++event) {
            QJsonObject row;
            row[QStringLiteral("day")] = day.key();
            row[QStringLiteral("name")] = event.key();
            row[QStringLiteral("count")] = event.value();
            usage << row;
        }
    }

    const auto pending = [files](const QString &folder, const QString &prefix, QJsonArray *into) {
        for (const QString &path : waiting(folder, prefix + QStringLiteral("*.json")).mid(0, kMostPerReport)) {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly))
                continue;
            QJsonObject item = QJsonDocument::fromJson(file.readAll()).object();
            if (item.isEmpty())
                continue;
            item[QStringLiteral("id")] = idOf(path, prefix);
            *into << item;
            if (files)
                *files << path;
        }
    };
    QJsonArray crashes, bugs;
    pending(crashDirectory, QStringLiteral("crash-"), &crashes);
    pending(directory, QStringLiteral("bug-"), &bugs);

    QJsonObject report;
    report[QStringLiteral("schema")] = 2;
    report[QStringLiteral("install")] = install;
    report[QStringLiteral("usage")] = usage;
    report[QStringLiteral("crashes")] = crashes;
    report[QStringLiteral("bugs")] = bugs;
    return QJsonDocument(report).toJson(QJsonDocument::Indented);
}

void Telemetry::send()
{
    if (!current.enabled || sending || !current.problem().isEmpty())
        return;
    save();
    QStringList files;
    const QByteArray body = build(&files);
    sending = true;
    QNetworkReply *reply = network->post(requestFor(current, "/api/v1/report"), body);
    connect(reply, &QNetworkReply::finished, this, [this, reply, files]() { finished(reply, files); });
}

void Telemetry::finished(QNetworkReply *reply, const QStringList &files)
{
    sending = false;
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool ok = reply->error() == QNetworkReply::NoError && status == 200;
    if (ok) {
        for (const QString &file : files) {
            if (QFileInfo(file).fileName().startsWith(QLatin1String("crash-")))
                QFile::rename(file, file + QStringLiteral(".enviado"));     // stays here, and won't go twice
            else
                QFile::remove(file);
        }
        forgetOldDays(kResentDays);
        save();
    } else {
        qCInfo(lcApp).noquote() << "Telemetry not sent, it stays for later:" << status << reply->errorString();
        // a server down for good mustn't fill the disk
        const QStringList kept = waiting(directory, QStringLiteral("bug-*.json"));
        for (int i = 0; i < kept.size() - kMostWaiting; ++i)
            QFile::remove(kept.at(i));
    }
    const QStringList crashFiles = waiting(crashDirectory, QStringLiteral("crash-*"));
    for (int i = 0; i < crashFiles.size() - kCrashesKept; ++i)
        QFile::remove(crashFiles.at(i));
    reply->deleteLater();
    emit sent(ok);
}

void Telemetry::ping(const TelemetrySettings &settings, const std::function<void(bool, const QString &)> &done)
{
    QNetworkReply *reply = network->post(requestFor(settings, "/api/v1/ping"), QByteArray("{}"));
    connect(reply, &QNetworkReply::finished, this, [reply, done]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() == QNetworkReply::NoError && status == 200)
            done(true, QCoreApplication::translate("Telemetry", "Conexión correcta."));
        else if (status == 401)
            done(false, QCoreApplication::translate("Telemetry", "El servidor rechazó la clave de envío."));
        else
            done(false, QCoreApplication::translate("Telemetry", "No se pudo conectar: %1").arg(reply->errorString()));
        reply->deleteLater();
    });
}
