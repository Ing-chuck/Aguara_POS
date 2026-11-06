#include "databasesettingsdialog.h"

#include <aguarastyle.h>
#include <formcheck.h>

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
    setWindowTitle(tr("Conexión a los datos - AguaraPOS"));

    backend = new QComboBox;
    backend->setObjectName(QStringLiteral("databaseBackend"));
    backend->addItem(tr("Este equipo (un solo puesto)"), int(DatabaseSettings::SQLite));
    backend->addItem(tr("Servidor en la red (varios puestos)"), int(DatabaseSettings::PostgreSQL));

    // SQLite page
    sqlitePath = new QLineEdit(QDir::toNativeSeparators(
        s.sqlitePath.isEmpty() ? DatabaseSettings::defaultSqlitePath() : s.sqlitePath));
    sqlitePath->setObjectName(QStringLiteral("databaseFile"));
    auto browse = new QPushButton(tr("Examinar..."));
    auto pathRow = new QHBoxLayout;
    pathRow->setContentsMargins(0, 0, 0, 0);
    pathRow->addWidget(sqlitePath);
    pathRow->addWidget(browse);
    auto sqlitePage = new QWidget;
    auto sqliteForm = new QFormLayout(sqlitePage);
    sqliteForm->addRow(AguaraForm::required(tr("Archivo:")), pathRow);

    // PostgreSQL page
    host = new QLineEdit(s.host);
    host->setObjectName(QStringLiteral("databaseHost"));
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
    pgForm->addRow(AguaraForm::required(tr("Servidor:")), host);
    pgForm->addRow(tr("Puerto:"), port);
    pgForm->addRow(AguaraForm::required(tr("Base de datos:")), name);
    pgForm->addRow(AguaraForm::required(tr("Usuario:")), user);
    pgForm->addRow(tr("Contraseña:"), password);

    pages = new QStackedWidget;
    pages->addWidget(sqlitePage);
    pages->addWidget(pgPage);

    notice = new QLabel;
    notice->setObjectName(QStringLiteral("databaseNotice"));
    notice->setWordWrap(true);
    AguaraStyle::setRole(notice, AguaraStyle::Warning);
    notice->hide();

    status = new QLabel;
    status->setObjectName(QStringLiteral("databaseStatus"));
    status->setWordWrap(true);
    status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    status->hide();
    if (!error.isEmpty())
        showStatus(error, AguaraStyle::Error);

    auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    okButton = buttons->button(QDialogButtonBox::Ok);
    okButton->setText(tr("Conectar"));
    okButton->setDefault(true);
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancelar"));
    auto test = new QPushButton(AguaraStyle::icon(QStringLiteral("database")), tr("Probar conexión"));
    test->setAutoDefault(false);
    browse->setAutoDefault(false);

    auto title = new QLabel(tr("¿Dónde están los datos?"));
    AguaraStyle::setRole(title, AguaraStyle::Title);
    auto subtitle = new QLabel(tr("Un solo puesto guarda los datos en este equipo. Para que varias cajas "
                                  "compartan los datos, use un servidor en la red."));
    subtitle->setWordWrap(true);
    AguaraStyle::setRole(subtitle, AguaraStyle::Subtitle);

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 18);
    layout->setSpacing(12);
    layout->addWidget(title);
    layout->addWidget(subtitle);
    layout->addSpacing(4);
    auto top = new QFormLayout;
    top->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    top->addRow(tr("Datos en:"), backend);
    layout->addLayout(top);
    layout->addWidget(pages);
    layout->addWidget(notice);
    layout->addWidget(status);
    layout->addStretch();
    auto buttonRow = new QHBoxLayout;
    buttonRow->addWidget(test);         // checks, doesn't close: apart from Conectar / Cancelar
    buttonRow->addStretch();
    buttonRow->addWidget(buttons);
    layout->addLayout(buttonRow);
    for (QFormLayout *form : { top, sqliteForm, pgForm }) {
        form->setHorizontalSpacing(12);
        form->setVerticalSpacing(10);
        form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    }
    // the labels of the three forms line up
    sqliteForm->setContentsMargins(0, 0, 0, 0);
    pgForm->setContentsMargins(0, 0, 0, 0);

    connect(backend, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &DatabaseSettingsDialog::onBackendChanged);
    connect(browse, &QPushButton::clicked, this, &DatabaseSettingsDialog::onBrowse);
    connect(test, &QPushButton::clicked, this, &DatabaseSettingsDialog::onTest);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    backend->setCurrentIndex(backend->findData(int(s.backend)));
    onBackendChanged(backend->currentIndex());
    resize(540, sizeHint().height());
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

void DatabaseSettingsDialog::setChangingRunningProgram(const QString &text)
{
    mustConnect = true;
    okButton->setText(tr("Guardar"));
    notice->setText(text);
    notice->show();
}

/// What a connection can't be tried without. The password stays optional: the
/// server may not ask for one.
bool DatabaseSettingsDialog::complete()
{
    const auto missing = [this](QLineEdit *field, const QString &text) {
        if (!field->text().trimmed().isEmpty())
            return false;
        showStatus(text, AguaraStyle::Error);
        field->setFocus();
        return true;
    };
    if (DatabaseSettings::Backend(backend->currentData().toInt()) == DatabaseSettings::SQLite)
        return !missing(sqlitePath, tr("Indique el archivo de datos."));
    return !missing(host, tr("Escriba la dirección o el nombre del servidor."))
        && !missing(name, tr("Escriba el nombre de la base de datos."))
        && !missing(user, tr("Escriba el usuario de la base de datos."));
}

void DatabaseSettingsDialog::accept()
{
    if (mustConnect ? tryConnection() : complete())
        QDialog::accept();
}

void DatabaseSettingsDialog::onTest()
{
    tryConnection();
}

bool DatabaseSettingsDialog::tryConnection()
{
    if (!complete())
        return false;
    showStatus(tr("Conectando..."), AguaraStyle::Info);
    status->repaint();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    bool ok;
    QString error;
    {
        Database probe(QStringLiteral("aguara-settings-test"));
        ok = probe.open(settings());
        error = probe.lastError();
    }
    QApplication::restoreOverrideCursor();
    if (ok)
        showStatus(tr("Conexión correcta."), AguaraStyle::Success);
    else
        showStatus(error, AguaraStyle::Error);
    return ok;
}

void DatabaseSettingsDialog::showStatus(const QString &text, const char *role)
{
    status->setText(text);
    AguaraStyle::setRole(status, role);
    status->show();
}
