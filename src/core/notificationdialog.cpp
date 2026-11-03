#include "notificationdialog.h"
#include "ui_notificationdialog.h"

#include <aguarastyle.h>

#include <QHash>
#include <QStyle>
#include <QTime>

using AguaraStyle::Color;

NotificationDialog::NotificationDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::NotificationDialog)
{
    ui->setupUi(this);
    setAttribute(Qt::WA_DeleteOnClose);     // Dismiss (wired to close() in the .ui) deletes the card
    setWindowFlags(Qt::Widget);             // a card inside the page, not a window

    ui->dismissButton->setIcon(AguaraStyle::icon(QStringLiteral("close"), AguaraStyle::color(Color::TextMuted)));
    ui->dismissButton->setIconSize(QSize(16, 16));
    ui->dismissButton->setCursor(Qt::PointingHandCursor);

    lifetimeTimer.setSingleShot(true);
    connect(&lifetimeTimer, &QTimer::timeout, this, &QWidget::close);

    setPriority(MsgPriority::PriorityLow);
}

NotificationDialog::~NotificationDialog()
{
    delete ui;
}

void NotificationDialog::setMessage(const ModuleMsg &msg)
{
    sender = msg.senderName;
    setTitle(msg.title);
    setMsg(msg.message);
    setPriority(msg.priority);
    setLifetime(msg.lifetime_seconds);
    updateMeta();
}

void NotificationDialog::setTitle(QString title) {
    ui->title->setText(title);
}

void NotificationDialog::setMsg(QString msg) {
    ui->message->setText(msg);
    ui->message->setVisible(!msg.isEmpty());
}

QString NotificationDialog::levelFor(MsgPriority priority)
{
    switch (priority) {
    case PriorityVeryHigh: return QStringLiteral("danger");
    case PriorityHigh:     return QStringLiteral("warning");
    case PriorityNormal:   return QStringLiteral("info");
    case PriorityLow:      return QStringLiteral("success");
    case PriorityVeryLow:  return QStringLiteral("neutral");
    }
    return QStringLiteral("neutral");
}

void NotificationDialog::setPriority(MsgPriority pri) {
    priority = pri;
    const QString level = levelFor(pri);
    static const QHash<QString, Color> colors = {
        { QStringLiteral("danger"), Color::Danger },   { QStringLiteral("warning"), Color::Warning },
        { QStringLiteral("info"), Color::Info },       { QStringLiteral("success"), Color::Success },
        { QStringLiteral("neutral"), Color::Neutral },
    };
    ui->levelIcon->setPixmap(AguaraStyle::icon(level, AguaraStyle::color(colors.value(level))).pixmap(22, 22));
    setProperty("level", level);
    style()->unpolish(this);
    style()->polish(this);
    update();
}

void NotificationDialog::setLifetime(double seconds) {
    if (seconds > 0)
        lifetimeTimer.start(int(seconds * 1000));   // restarts if the message was updated
    else
        lifetimeTimer.stop();
}

/// "Inventario · 15:42": who sent it and when it last changed
void NotificationDialog::updateMeta()
{
    const QString time = QTime::currentTime().toString(QStringLiteral("HH:mm"));
    ui->meta->setText(sender.isEmpty() ? time : QStringLiteral("%1 · %2").arg(sender, time));
}
