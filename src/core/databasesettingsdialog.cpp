#include "databasesettingsdialog.h"

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

DatabaseSettingsDialog::DatabaseSettingsDialog(const DatabaseSettings &s, const QString &error, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Base de datos - AguaraPOS"));

    backend = new QComboBox;
    backend->addItem(tr("Este equipo (un solo puesto)"), int(DatabaseSettings::SQLite));
    backend->addItem(tr("Servidor en la red (varios puestos)"), int(DatabaseSettings::PostgreSQL));

    // SQLite page
    sqlitePath = new QLineEdit(QDir::toNativeSeparators(
        s.sqlitePath.isEmpty() ? DatabaseSettings::defaultSqlitePath() : s.sqlitePath));
    auto browse = new QPushButton(tr("Examinar..."));
    auto pathRow = new QHBoxLayout;
    pathRow->setContentsMargins(0, 0, 0, 0);
    pathRow->addWidget(sqlitePath);
    pathRow->addWidget(browse);
    auto sqlitePage = new QWidget;
    auto sqliteForm = new QFormLayout(sqlitePage);
    sqliteForm->addRow(tr("Archivo:"), pathRow);

    // PostgreSQL page
    host = new QLineEdit(s.host);
    host->setPlaceholderText(tr("ej. 192.168.1.10 o nombre del equipo"));
    port = new QSpinBox;
    port->setRange(1, 65535);
    port->setValue(s.port);
    name = new QLineEdit(s.name);
    user = new QLineEdit(s.user);
    password = new QLineEdit(s.password);
    password->setEchoMode(QLineEdit::Password);
    auto pgPage = new QWidget;
    auto pgForm = new QFormLayout(pgPage);
    pgForm->addRow(tr("Servidor:"), host);
    pgForm->addRow(tr("Puerto:"), port);
    pgForm->addRow(tr("Base de datos:"), name);
    pgForm->addRow(tr("Usuario:"), user);
    pgForm->addRow(tr("Contraseña:"), password);

    pages = new QStackedWidget;
    pages->addWidget(sqlitePage);
    pages->addWidget(pgPage);

    status = new QLabel;
    status->setWordWrap(true);
    status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    if (!error.isEmpty())
        status->setText(tr("No se pudo abrir la base de datos:\n%1").arg(error));

    auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    auto test = buttons->addButton(tr("Probar conexion"), QDialogButtonBox::ActionRole);

    auto layout = new QVBoxLayout(this);
    auto top = new QFormLayout;
    top->addRow(tr("Datos en:"), backend);
    layout->addLayout(top);
    layout->addWidget(pages);
    layout->addWidget(status);
    layout->addStretch();
    layout->addWidget(buttons);

    connect(backend, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &DatabaseSettingsDialog::onBackendChanged);
    connect(browse, &QPushButton::clicked, this, &DatabaseSettingsDialog::onBrowse);
    connect(test, &QPushButton::clicked, this, &DatabaseSettingsDialog::onTest);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    backend->setCurrentIndex(backend->findData(int(s.backend)));
    onBackendChanged(backend->currentIndex());
    resize(480, sizeHint().height());
}

DatabaseSettings DatabaseSettingsDialog::settings() const
{
    DatabaseSettings s;
    s.backend    = DatabaseSettings::Backend(backend->currentData().toInt());
    s.sqlitePath = QDir::fromNativeSeparators(sqlitePath->text().trimmed());
    if (QDir::cleanPath(s.sqlitePath) == QDir::cleanPath(DatabaseSettings::defaultSqlitePath()))
        s.sqlitePath.clear();   // keep following the default location
    s.host     = host->text().trimmed();
    s.port     = port->value();
    s.name     = name->text().trimmed();
    s.user     = user->text().trimmed();
    s.password = password->text();
    return s;
}

void DatabaseSettingsDialog::onBackendChanged(int index)
{
    pages->setCurrentIndex(index);
}

void DatabaseSettingsDialog::onBrowse()
{
    const QString file = QFileDialog::getSaveFileName(this, tr("Archivo de datos"), sqlitePath->text(),
                                                      tr("Base de datos SQLite (*.db)"), nullptr,
                                                      QFileDialog::DontConfirmOverwrite);
    if (!file.isEmpty())
        sqlitePath->setText(QDir::toNativeSeparators(file));
}

void DatabaseSettingsDialog::onTest()
{
    status->setText(tr("Conectando..."));
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString result;
    {
        Database probe(QStringLiteral("aguara-settings-test"));
        result = probe.open(settings()) ? tr("Conexion correcta.")
                                        : tr("Error:\n%1").arg(probe.lastError());
    }
    QApplication::restoreOverrideCursor();
    status->setText(result);
}
