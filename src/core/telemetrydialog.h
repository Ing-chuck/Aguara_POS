#ifndef TELEMETRYDIALOG_H
#define TELEMETRYDIALOG_H

#include <QDialog>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class Telemetry;
struct TelemetrySettings;

/// Preferencias > Telemetría: whether this computer sends anything, where to,
/// and exactly what.
class TelemetryDialog : public QDialog
{
    Q_OBJECT
public:
    TelemetryDialog(Telemetry *telemetry, QWidget *parent = nullptr);
    void accept() override;

private:
    TelemetrySettings chosen() const;
    void testConnection();
    void showReport();
    void updateFields();
    void showStatus(const QString &text, const char *role);

    Telemetry *telemetry;
    QCheckBox *enabled;
    QLineEdit *endpoint;
    QLineEdit *key;
    QLineEdit *installName;
    QLabel *status;
};

/// Ayuda > Reportar un problema
class BugReportDialog : public QDialog
{
    Q_OBJECT
public:
    BugReportDialog(Telemetry *telemetry, QWidget *parent = nullptr);
    void accept() override;

private:
    Telemetry *telemetry;
    QPlainTextEdit *text;
    QLineEdit *contact;
    QLabel *error;
};

#endif // TELEMETRYDIALOG_H
