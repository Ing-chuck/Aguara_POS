#include "formcheck.h"
#include "aguarastyle.h"

#include <QAbstractButton>
#include <QCoreApplication>
#include <QDateEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

#include <algorithm>

namespace AguaraForm {

QString required(const QString &label)
{
    QString text = label;
    if (text.endsWith(QLatin1Char(':'))) {
        text.chop(1);
        return text + QStringLiteral(" *:");
    }
    return text + QStringLiteral(" *");
}

QLabel *requiredNote()
{
    auto note = new QLabel(QObject::tr("* Dato obligatorio"));
    AguaraStyle::setRole(note, AguaraStyle::Muted);
    return note;
}

QLabel *errorLabel(const QString &objectName)
{
    auto label = new QLabel;
    label->setObjectName(objectName);
    label->setWordWrap(true);
    AguaraStyle::setRole(label, AguaraStyle::Error);
    label->hide();
    return label;
}

void showError(QLabel *label, const QString &text, QWidget *field)
{
    label->setText(text);
    label->show();
    if (!field)
        return;
    field->setFocus();
    if (auto edit = qobject_cast<QLineEdit *>(field))
        edit->selectAll();
}

bool validPhone(const QString &phone)
{
    const QString text = phone.trimmed();
    if (text.isEmpty())
        return true;
    static const QRegularExpression shape(QStringLiteral("^\\+?[0-9 ()./-]+$"));
    const int digits = int(std::count_if(text.cbegin(), text.cend(), [](QChar c) { return c.isDigit(); }));
    return shape.match(text).hasMatch() && digits >= 6 && digits <= 30;
}

bool validEmail(const QString &email)
{
    const QString text = email.trimmed();
    static const QRegularExpression shape(QStringLiteral("^[^@\\s]+@[^@\\s.]+(\\.[^@\\s.]+)+$"));
    return text.isEmpty() || shape.match(text).hasMatch();
}

bool validWeb(const QString &web)
{
    const QString text = web.trimmed();
    static const QRegularExpression shape(QStringLiteral("^(https?://)?[^\\s/.]+(\\.[^\\s/.]+)+(/\\S*)?$"),
                                          QRegularExpression::CaseInsensitiveOption);
    return text.isEmpty() || shape.match(text).hasMatch();
}

bool validUsername(const QString &username)
{
    static const QRegularExpression space(QStringLiteral("\\s"));
    return !username.trimmed().contains(space);
}

bool validCode(const QString &code)
{
    static const QRegularExpression shape(QStringLiteral("^[0-9A-Za-z-]{1,20}$"));
    return shape.match(code.trimmed()).hasMatch();
}

bool fitsUnit(Quantity quantity, int decimals)
{
    Quantity step = AguaraQuantity::One;
    for (int i = 0; i < decimals && step > 1; ++i)
        step /= 10;
    return quantity % step == 0;
}

bool confirmRemoval(QWidget *parent, const QString &title, const QString &text, const QString &action, const QString &keep)
{
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("confirmRemoval"));
    dialog.setWindowTitle(title);
    dialog.setMinimumWidth(400);
    auto layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(24, 20, 24, 18);
    layout->setSpacing(12);
    auto heading = new QLabel(title);
    AguaraStyle::setRole(heading, AguaraStyle::Title);
    layout->addWidget(heading);
    auto body = new QLabel(text);
    body->setObjectName(QStringLiteral("confirmText"));
    body->setWordWrap(true);
    AguaraStyle::setRole(body, AguaraStyle::Subtitle);
    layout->addWidget(body);

    auto buttons = new QDialogButtonBox;
    layout->addWidget(buttons);
    QPushButton *yes = buttons->addButton(action, QDialogButtonBox::DestructiveRole);
    yes->setObjectName(QStringLiteral("confirmYes"));
    AguaraStyle::setRole(yes, AguaraStyle::Danger);
    QPushButton *no = buttons->addButton(keep.isEmpty() ? QObject::tr("Cancelar") : keep, QDialogButtonBox::RejectRole);
    no->setObjectName(QStringLiteral("confirmNo"));
    no->setDefault(true);
    no->setFocus();
    QObject::connect(yes, &QPushButton::clicked, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    return dialog.exec() == QDialog::Accepted;
}

void keepInOrder(QDateEdit *from, QDateEdit *to)
{
    QObject::connect(from, &QDateEdit::dateChanged, to, [to](const QDate &date) {
        if (to->date() < date)
            to->setDate(date);
    });
    QObject::connect(to, &QDateEdit::dateChanged, from, [from](const QDate &date) {
        if (from->date() > date)
            from->setDate(date);
    });
}

} // namespace AguaraForm
