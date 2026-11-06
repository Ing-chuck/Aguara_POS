#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <QMap>
#include <QObject>
#include <QStringList>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

/// How this computer sends telemetry. In aguara.ini, off until an administrator
/// turns it on in Preferencias > Telemetría. The key goes encrypted, like the
/// database password.
struct TelemetrySettings {
    bool    enabled = false;
    QString endpoint;           ///< https://server:port, the root of the server
    QString key;
    QString installName;        ///< how the panel names this computer; optional
    QString installId;          ///< random, created once: it says nothing about the shop

    static TelemetrySettings load();
    void save() const;

    /// Why it can't be used as it is. Empty if it can.
    QString problem() const;
};

/*
    Usage statistics, crash reports and bug reports, to a server of ours (a
    separate project, Aguara_Telemetry).

    What leaves this computer is only what report() builds, and Preferencias >
    Telemetría shows exactly that. Usage is a day, the name of an event and a
    count: never customers, articles, amounts, users or computer names. An event
    name that isn't a plain identifier is dropped here, so a text can't slip
    through as one. Crash reports are an error code and places in the code, with
    no memory and no logs, which can hold real data.

    The till never waits for it: everything goes in the background, and what can't
    be sent waits on disk. Sending twice is harmless. A day's count goes as a
    total, so the server keeps the highest, and reports go with an id.
*/
class Telemetry : public QObject
{
    Q_OBJECT
public:
    /// `crashes`: where CrashHandler leaves them. Empty: `directory`.
    explicit Telemetry(const QString &directory, const QString &crashes = QString(), QObject *parent = nullptr);
    ~Telemetry() override;

    /// %APPDATA%\Aguara\AguaraPOS\telemetry
    static QString defaultDirectory();

    /// One more of this event: a line in the local activity log, and for
    /// telemetry, if it's on, one more today. Anything that isn't a plain
    /// identifier goes to neither.
    static void count(const QString &event);

    void configure(const TelemetrySettings &settings);
    TelemetrySettings settings() const { return current; }
    bool isEnabled() const { return current.enabled; }

    /// What the report says about this installation.
    void setInstall(const QString &backend, const QStringList &modules);

    /// Goes with the next report, or waits for the server.
    void reportBug(const QString &text, const QString &contact);

    /// Exactly what would be sent now.
    QByteArray report() const { return build(nullptr); }

    void send();

    /// Tries a server and its key.
    void ping(const TelemetrySettings &settings, const std::function<void(bool, const QString &)> &done);

signals:
    void sent(bool ok);

private:
    void addEvent(const QString &event);
    void load();
    void save() const;
    void forgetOldDays(int keep);
    QByteArray build(QStringList *files) const;
    void finished(QNetworkReply *reply, const QStringList &files);

    QString directory;
    QString crashDirectory;
    TelemetrySettings current;
    QString backend;
    QStringList modules;
    QMap<QString, QMap<QString, int>> days;     ///< day -> event -> how many
    QNetworkAccessManager *network;
    QTimer *sender;
    QTimer *saver;
    bool sending = false;
};

#endif // TELEMETRY_H
