#include "telemetrydialog.h"
#include "telemetry.h"

#include <aguarastyle.h>
#include <formcheck.h>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

QVBoxLayout *heading(QDialog *dialog, const QString &title, const QString &subtitle)
{
    dialog->setWindowTitle(title);
    auto layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(24, 20, 24, 18);
    layout->setSpacing(12);
    auto label = new QLabel(title);
    AguaraStyle::setRole(label, AguaraStyle::Title);
    layout->addWidget(label);
    auto sub = new QLabel(subtitle);
    sub->setWordWrap(true);
    AguaraStyle::setRole(sub, AguaraStyle::Subtitle);
    layout->addWidget(sub);
    return layout;
}

} // namespace

// --- TelemetryDialog -----------------------------------------------------------

TelemetryDialog::TelemetryDialog(Telemetry *telemetry, QWidget *parent)
    : QDialog(parent), telemetry(telemetry)
{
    auto layout = heading(this, tr("Telemetría"),
        tr("Ayuda a mejorar el programa durante la prueba. Este equipo envía cuántas veces se usa cada módulo y función, "
           "los cierres inesperados y los problemas que el personal reporte. Nunca envía clientes, artículos, montos, "
           "usuarios ni nombres de equipos."));
    setMinimumWidth(520);

    const TelemetrySettings settings = telemetry->settings();
    enabled = new QCheckBox(tr("Enviar datos de uso de este equipo"));
    enabled->setObjectName(QStringLiteral("telemetryEnabled"));
    enabled->setChecked(settings.enabled);
    layout->addWidget(enabled);

    endpoint = new QLineEdit(settings.endpoint);
    endpoint->setObjectName(QStringLiteral("telemetryEndpoint"));
    endpoint->setPlaceholderText(tr("https://servidor:8787"));
    key = new QLineEdit(settings.key);
    key->setObjectName(QStringLiteral("telemetryKey"));
    key->setEchoMode(QLineEdit::Password);
    installName = new QLineEdit(settings.installName);
    installName->setObjectName(QStringLiteral("telemetryName"));
    installName->setMaxLength(80);
    installName->setPlaceholderText(tr("Opcional: cómo aparece en el panel, por ejemplo Piloto"));

    auto form = new QFormLayout;
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(10);
    form->addRow(AguaraForm::required(tr("Dirección del servidor:")), endpoint);
    form->addRow(AguaraForm::required(tr("Clave de envío:")), key);
    form->addRow(tr("Nombre de esta instalación:"), installName);
    layout->addLayout(form);

    status = new QLabel;
    status->setObjectName(QStringLiteral("telemetryStatus"));
    status->setWordWrap(true);
    status->hide();
    layout->addWidget(status);

    auto buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setText(tr("Guardar"));
    buttons->button(QDialogButtonBox::Save)->setDefault(true);
    AguaraStyle::setRole(buttons->button(QDialogButtonBox::Save), AguaraStyle::Primary);
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancelar"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto test = new QPushButton(tr("Probar conexión"));
    test->setObjectName(QStringLiteral("telemetryTest"));
    test->setAutoDefault(false);
    auto show = new QPushButton(tr("Ver lo que se envía"));
    show->setObjectName(QStringLiteral("telemetryShow"));
    show->setAutoDefault(false);
    auto row = new QHBoxLayout;
    row->addWidget(test);
    row->addWidget(show);
    row->addStretch();
    row->addWidget(buttons);
    layout->addLayout(row);

    connect(enabled, &QCheckBox::toggled, this, &TelemetryDialog::updateFields);
    connect(test, &QPushButton::clicked, this, &TelemetryDialog::testConnection);
    connect(show, &QPushButton::clicked, this, &TelemetryDialog::showReport);
    updateFields();
}

TelemetrySettings TelemetryDialog::chosen() const
{
    TelemetrySettings s = telemetry->settings();
    s.enabled = enabled->isChecked();
    s.endpoint = endpoint->text().trimmed();
    s.key = key->text().trimmed();
    s.installName = installName->text().simplified();
    return s;
}

void TelemetryDialog::updateFields()
{
    const bool on = enabled->isChecked();
    endpoint->setEnabled(on);
    key->setEnabled(on);
    installName->setEnabled(on);
}

void TelemetryDialog::showStatus(const QString &text, const char *role)
{
    status->setText(text);
    AguaraStyle::setRole(status, role);
    status->show();
}

void TelemetryDialog::testConnection()
{
    const TelemetrySettings s = chosen();
    const QString problem = s.problem();
    if (!problem.isEmpty()) {
        showStatus(problem, AguaraStyle::Error);
        return;
    }
    showStatus(tr("Conectando..."), AguaraStyle::Info);
    QPointer<TelemetryDialog> self(this);
    telemetry->ping(s, [self](bool ok, const QString &text) {
        if (self)
            self->showStatus(text, ok ? AguaraStyle::Success : AguaraStyle::Error);
    });
}

void TelemetryDialog::showReport()
{
    QDialog dialog(this);
    auto layout = heading(&dialog, tr("Lo que se envía"),
        tr("Este es el contenido exacto del próximo envío. La clave de envío va aparte, y aquí no se muestra."));
    auto text = new QPlainTextEdit(QString::fromUtf8(telemetry->report()));
    text->setObjectName(QStringLiteral("telemetryReport"));
    text->setReadOnly(true);
    text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    layout->addWidget(text, 1);
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->button(QDialogButtonBox::Close)->setText(tr("Cerrar"));
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.resize(640, 520);
    dialog.exec();
}

void TelemetryDialog::accept()
{
    const TelemetrySettings s = chosen();
    if (s.enabled) {
        const QString problem = s.problem();
        if (!problem.isEmpty()) {
            showStatus(problem, AguaraStyle::Error);
            return;
        }
    }
    telemetry->configure(s);
    telemetry->settings().save();
    QDialog::accept();
}

// --- BugReportDialog -----------------------------------------------------------

BugReportDialog::BugReportDialog(Telemetry *telemetry, QWidget *parent)
    : QDialog(parent), telemetry(telemetry)
{
    auto layout = heading(this, tr("Reportar un problema"),
        tr("Cuente qué estaba haciendo y qué pasó. Por favor, no escriba datos de clientes ni contraseñas."));
    setMinimumWidth(480);

    text = new QPlainTextEdit;
    text->setObjectName(QStringLiteral("bugText"));
    text->setPlaceholderText(tr("Por ejemplo: al cobrar con tarjeta, el programa quedó trabado."));
    text->setTabChangesFocus(true);
    layout->addWidget(new QLabel(AguaraForm::required(tr("Qué pasó:"))));
    layout->addWidget(text, 1);

    contact = new QLineEdit;
    contact->setObjectName(QStringLiteral("bugContact"));
    contact->setMaxLength(120);
    contact->setPlaceholderText(tr("Opcional: a quién preguntarle, si hace falta"));
    layout->addWidget(new QLabel(tr("Contacto:")));
    layout->addWidget(contact);

    error = AguaraForm::errorLabel(QStringLiteral("bugError"));
    layout->addWidget(error);

    auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Enviar"));
    AguaraStyle::setRole(buttons->button(QDialogButtonBox::Ok), AguaraStyle::Primary);
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancelar"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    text->setFocus();
}

void BugReportDialog::accept()
{
    const QString written = text->toPlainText().trimmed();
    if (written.isEmpty()) {
        AguaraForm::showError(error, tr("Cuente qué pasó."), text);
        return;
    }
    if (written.size() > 4000) {
        AguaraForm::showError(error, tr("El texto es demasiado largo: use hasta 4000 caracteres."), text);
        return;
    }
    telemetry->reportBug(written, contact->text());
    telemetry->send();
    QDialog::accept();
}
