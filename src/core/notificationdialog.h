#ifndef NOTIFICATIONDIALOG_H
#define NOTIFICATIONDIALOG_H

#include "messages.h"

#include <QDialog>
#include <QTimer>

namespace Ui {
class NotificationDialog;
}

/// One notification card on the notifications page: an icon and a colored
/// stripe for the priority, title, sender and time, and the message.
/// Deletes itself when dismissed or when its lifetime runs out.
class NotificationDialog : public QDialog
{
    Q_OBJECT

public:
    explicit NotificationDialog(QWidget *parent = nullptr);
    ~NotificationDialog();

    /// Show this message (title, text, priority, lifetime). Calling it again
    /// (same sender+id) updates the card and its time.
    void setMessage(const ModuleMsg &msg);

    void setTitle(QString title);
    void setMsg(QString msg);
    void setPriority(MsgPriority pri);
    /// Close after this many seconds; negative = until dismissed.
    void setLifetime(double seconds);

    /// "danger", "warning", "info", "success" or "neutral"
    static QString levelFor(MsgPriority priority);

private:
    MsgPriority priority = PriorityNormal;
    QString sender;
    Ui::NotificationDialog *ui;
    QTimer lifetimeTimer;

    void updateMeta();
};

#endif // NOTIFICATIONDIALOG_H
