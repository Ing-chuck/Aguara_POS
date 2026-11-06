#ifndef DATABASESETTINGSDIALOG_H
#define DATABASESETTINGSDIALOG_H

#include "database.h"

#include <QDialog>

QT_BEGIN_NAMESPACE
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;
QT_END_NAMESPACE

/// Choose where the data lives: this computer (SQLite) or a server on the
/// LAN (PostgreSQL). Shown when the database can't be opened, on demand with
/// --configure-database, and from Preferencias.
class DatabaseSettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit DatabaseSettingsDialog(const DatabaseSettings &settings,
                                    const QString &error = QString(),
                                    QWidget *parent = nullptr);

    DatabaseSettings settings() const;

    /// The program is already running on other settings: the button saves
    /// instead of connecting, only a connection that works is accepted, and
    /// `notice` says what happens next.
    void setChangingRunningProgram(const QString &notice);

    void accept() override;

private slots:
    void onBackendChanged(int index);
    void onBrowse();
    void onTest();

private:
    void showStatus(const QString &text, const char *role);
    bool complete();
    bool tryConnection();

private:
    QComboBox      *backend;
    QStackedWidget *pages;
    QLineEdit      *sqlitePath;
    QLineEdit      *host;
    QSpinBox       *port;
    QLineEdit      *name;
    QLineEdit      *user;
    QLineEdit      *password;
    QLabel         *status;
    QLabel         *notice;
    QPushButton    *okButton;
    bool            mustConnect = false;
};

#endif // DATABASESETTINGSDIALOG_H
