#include "sessiondialogs.h"
#include "sqluserservice.h"

#include <aguarastyle.h>

#include <QApplication>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

/// Label above a field, as in the rest of the forms
QLineEdit *addField(QVBoxLayout *layout, const QString &label, bool secret, const QString &objectName)
{
    auto text = new QLabel(label);
    auto edit = new QLineEdit;
    edit->setObjectName(objectName);
    if (secret)
        edit->setEchoMode(QLineEdit::Password);
    text->setBuddy(edit);
    layout->addWidget(text);
    layout->addWidget(edit);
    layout->addSpacing(6);
    return edit;
}

QLabel *addError(QVBoxLayout *layout)
{
    auto error = new QLabel;
    error->setObjectName(QStringLiteral("errorLabel"));
    error->setWordWrap(true);
    AguaraStyle::setRole(error, AguaraStyle::Error);
    error->hide();
    layout->addWidget(error);
    return error;
}

void showError(QLabel *label, const QString &text)
{
    label->setText(text);
    label->show();
}

QVBoxLayout *dialogLayout(QDialog *dialog, const QString &title, const QString &subtitle, bool logo)
{
    auto layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(28, 24, 28, 20);
    layout->setSpacing(4);
    if (logo) {
        auto image = new QLabel;
        image->setPixmap(AguaraStyle::logo().pixmap(56, 56));
        layout->addWidget(image, 0, Qt::AlignHCenter);
        layout->addSpacing(8);
    }
    auto heading = new QLabel(title);
    AguaraStyle::setRole(heading, AguaraStyle::Title);
    layout->addWidget(heading, 0, logo ? Qt::AlignHCenter : Qt::AlignLeft);
    if (!subtitle.isEmpty()) {
        auto sub = new QLabel(subtitle);
        sub->setWordWrap(true);
        sub->setAlignment(logo ? Qt::AlignHCenter : Qt::AlignLeft);
        AguaraStyle::setRole(sub, AguaraStyle::Subtitle);
        layout->addWidget(sub);
    }
    layout->addSpacing(12);
    dialog->setMinimumWidth(400);
    return layout;
}

QDialogButtonBox *addButtons(QDialog *dialog, QVBoxLayout *layout, const QString &ok, const QString &cancel)
{
    auto buttons = new QDialogButtonBox;
    auto accept = buttons->addButton(ok, QDialogButtonBox::AcceptRole);
    accept->setDefault(true);
    if (!cancel.isEmpty())
        buttons->addButton(cancel, QDialogButtonBox::RejectRole);
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    layout->addSpacing(8);
    layout->addWidget(buttons);
    return buttons;
}

} // namespace

// --- LoginDialog -------------------------------------------------------------

LoginDialog::LoginDialog(SqlUserService *users, Mode mode, QWidget *parent)
    : QDialog(parent), users(users), mode(mode)
{
    const auto current = users->currentUser();
    setWindowTitle(QStringLiteral("AguaraPOS"));
    QString title = tr("Iniciar sesión");
    QString subtitle = tr("Ingrese su usuario y contraseña");
    if (mode == Locked) {
        title = tr("Sesión bloqueada");
        subtitle = current ? tr("%1 dejó la sesión abierta. Ingrese la contraseña para seguir, u otro "
                                "usuario para cambiar de cajero.").arg(current->fullName) : QString();
    } else if (mode == Switch) {
        title = tr("Cambiar de usuario");
    }

    auto layout = dialogLayout(this, title, subtitle, true);
    username = addField(layout, tr("Usuario"), false, QStringLiteral("username"));
    password = addField(layout, tr("Contraseña"), true, QStringLiteral("password"));
    error = addError(layout);

    if (mode == Locked && current) {
        username->setText(current->username);
        password->setFocus();
    } else {
        username->setFocus();
    }

    addButtons(this, layout, mode == Switch ? tr("Cambiar") : tr("Ingresar"),
               mode == Switch ? tr("Cancelar") : tr("Salir del programa"));
}

void LoginDialog::accept()
{
    // a blank password is not an attempt: it must not count towards the lockout
    if (username->text().trimmed().isEmpty()) {
        showError(error, tr("Escriba su usuario."));
        username->setFocus();
        return;
    }
    if (password->text().isEmpty()) {
        showError(error, tr("Escriba su contraseña."));
        password->setFocus();
        return;
    }
    if (!users->login(username->text(), password->text())) {
        showError(error, users->lastError());
        password->clear();
        password->setFocus();
        return;
    }
    if (users->currentUser()->mustChangePassword) {
        ChangePasswordDialog change(users, true, this);
        if (change.exec() != QDialog::Accepted) {
            users->logout();
            showError(error, tr("Debe elegir una contraseña propia para poder ingresar."));
            password->clear();
            return;
        }
    }
    QDialog::accept();
}

void LoginDialog::forbidExit(const QString &reason)
{
    exitForbidden = true;
    auto box = findChild<QDialogButtonBox *>();
    for (QAbstractButton *button : box->buttons()) {
        if (box->buttonRole(button) == QDialogButtonBox::RejectRole)
            button->hide();
    }
    showError(error, reason);
}

void LoginDialog::reject()
{
    if (exitForbidden)
        return;
    if (mode == Locked && !qobject_cast<QDialogButtonBox *>(sender()))
        return;                 // Esc doesn't get past a locked session
    QDialog::reject();
}

// --- FirstAdminDialog --------------------------------------------------------

FirstAdminDialog::FirstAdminDialog(SqlUserService *users, QWidget *parent)
    : QDialog(parent), users(users)
{
    setWindowTitle(tr("Bienvenido a AguaraPOS"));
    auto layout = dialogLayout(this, tr("Bienvenido a AguaraPOS"),
        tr("Cree el usuario administrador. Con él podrá crear después los usuarios de los cajeros."), true);
    fullName = addField(layout, tr("Su nombre"), false, QStringLiteral("fullName"));
    username = addField(layout, tr("Usuario"), false, QStringLiteral("username"));
    username->setText(QStringLiteral("admin"));
    password = addField(layout, tr("Contraseña (al menos 6 caracteres)"), true, QStringLiteral("password"));
    repeat = addField(layout, tr("Repita la contraseña"), true, QStringLiteral("repeat"));
    error = addError(layout);
    addButtons(this, layout, tr("Crear administrador"), tr("Salir del programa"));
    fullName->setFocus();
}

void FirstAdminDialog::accept()
{
    if (password->text() != repeat->text()) {
        showError(error, tr("Las contraseñas no coinciden."));
        return;
    }
    if (!users->createFirstAdmin(username->text(), fullName->text(), password->text())) {
        showError(error, users->lastError());
        return;
    }
    QDialog::accept();
}

// --- ChangePasswordDialog ----------------------------------------------------

ChangePasswordDialog::ChangePasswordDialog(UserService *users, bool forced, QWidget *parent)
    : QDialog(parent), users(users)
{
    setWindowTitle(tr("Cambiar contraseña"));
    auto layout = dialogLayout(this, tr("Cambiar contraseña"),
        forced ? tr("Su contraseña fue asignada por un administrador. Elija una propia para continuar.")
               : QString(), false);
    current = addField(layout, tr("Contraseña actual"), true, QStringLiteral("current"));
    replacement = addField(layout, tr("Contraseña nueva (al menos 6 caracteres)"), true, QStringLiteral("replacement"));
    repeat = addField(layout, tr("Repita la contraseña nueva"), true, QStringLiteral("repeat"));
    error = addError(layout);
    addButtons(this, layout, tr("Guardar"), tr("Cancelar"));
    current->setFocus();
}

void ChangePasswordDialog::accept()
{
    if (current->text().isEmpty()) {
        showError(error, tr("Escriba su contraseña actual."));
        current->setFocus();
        return;
    }
    if (replacement->text() != repeat->text()) {
        showError(error, tr("Las contraseñas nuevas no coinciden."));
        return;
    }
    if (!users->changePassword(current->text(), replacement->text())) {
        showError(error, users->lastError());
        return;
    }
    QDialog::accept();
}

// --- supervisor ---------------------------------------------------------------

bool askSupervisor(const QString &permissionDescription, const QString &reason, const QString &errorText,
                   QWidget *parent, QString *username, QString *password)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QCoreApplication::translate("Session", "Autorización"));
    auto layout = dialogLayout(&dialog, QCoreApplication::translate("Session", "Autorización de un supervisor"),
        QStringLiteral("%1%2").arg(permissionDescription,
                                   reason.isEmpty() ? QString() : QStringLiteral("\n") + reason), false);
    auto user = addField(layout, QCoreApplication::translate("Session", "Usuario del supervisor"), false, QStringLiteral("username"));
    auto pass = addField(layout, QCoreApplication::translate("Session", "Contraseña"), true, QStringLiteral("password"));
    auto error = addError(layout);
    if (!errorText.isEmpty())
        showError(error, errorText);
    QDialogButtonBox *buttons = addButtons(&dialog, layout, QCoreApplication::translate("Session", "Autorizar"),
                                           QCoreApplication::translate("Session", "Cancelar"));
    QAbstractButton *authorize = buttons->buttons().constFirst();
    for (QAbstractButton *button : buttons->buttons()) {
        if (buttons->buttonRole(button) == QDialogButtonBox::AcceptRole)
            authorize = button;
    }
    // with a field blank there is nothing to check, and a failed try counts towards the lockout
    const auto complete = [user, pass, authorize]() {
        authorize->setEnabled(!user->text().trimmed().isEmpty() && !pass->text().isEmpty());
    };
    QObject::connect(user, &QLineEdit::textChanged, &dialog, complete);
    QObject::connect(pass, &QLineEdit::textChanged, &dialog, complete);
    complete();
    user->setFocus();
    if (dialog.exec() != QDialog::Accepted)
        return false;
    *username = user->text();
    *password = pass->text();
    return true;
}
