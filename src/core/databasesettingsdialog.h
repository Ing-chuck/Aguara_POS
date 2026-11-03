#ifndef DATABASESETTINGSDIALOG_H
#define DATABASESETTINGSDIALOG_H

#include "database.h"

#include <QDialog>

QT_BEGIN_NAMESPACE
class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QStackedWidget;
QT_END_NAMESPACE

/// Choose where the data lives: this computer (SQLite) or a server on the
/// LAN (PostgreSQL). Shown when the database can't be opened, or on demand
/// with --configure-database.
class DatabaseSettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit DatabaseSettingsDialog(const DatabaseSettings &settings,
                                    const QString &error = QString(),
                                    QWidget *parent = nullptr);

    DatabaseSettings settings() const;

private slots:
    void onBackendChanged(int index);
    void onBrowse();
    void onTest();

private:
    void showStatus(const QString &text, const char *role);

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
};

#endif // DATABASESETTINGSDIALOG_H
