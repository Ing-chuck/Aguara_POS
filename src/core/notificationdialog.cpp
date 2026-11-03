#include "notificationdialog.h"
#include "ui_notificationdialog.h"

NotificationDialog::NotificationDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::NotificationDialog)
{
    ui->setupUi(this);
    setAttribute(Qt::WA_DeleteOnClose);     // Dismiss (wired to close() in the .ui) deletes the card

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
    setTitle(msg.senderName.isEmpty() ? msg.title
                                      : QStringLiteral("%1 - %2").arg(msg.senderName, msg.title));
    setMsg(msg.message);
    setPriority(msg.priority);
    setLifetime(msg.lifetime_seconds);
}

void NotificationDialog::setTitle(QString title) {
    ui->title->setText(title);
}

void NotificationDialog::setMsg(QString msg) {
    ui->message->setText(msg);
}

void NotificationDialog::setPriority(MsgPriority pri) {
    priority = pri;
    changeColor();
}

void NotificationDialog::setLifetime(double seconds) {
    if (seconds > 0)
        lifetimeTimer.start(int(seconds * 1000));   // restarts if the message was updated
    else
        lifetimeTimer.stop();
}

void NotificationDialog::changeColor() {
    // Scoped to the card itself, so the button and labels keep their own style
    QString color;
    switch (priority) {
    case PriorityVeryLow:  color = "rgb(170, 170, 170)"; break;  // Grey
    case PriorityLow:      color = "rgb(0, 150, 0)";     break;  // Green
    case PriorityNormal:   color = "rgb(255, 150, 0)";   break;  // Orange
    case PriorityHigh:     color = "rgb(255, 255, 0)";   break;  // Yellow
    case PriorityVeryHigh: color = "rgb(255, 0, 0)";     break;  // Red
    }
    this->setStyleSheet(QStringLiteral("NotificationDialog { background: %1; }").arg(color));
}
