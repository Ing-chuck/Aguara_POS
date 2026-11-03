#ifndef NOTIFICATIONDIALOG_H
#define NOTIFICATIONDIALOG_H

#include "messages.h"

#include <QDialog>
#include <QTimer>

namespace Ui {
class NotificationDialog;
}

/// One notification card on the notifications page.
/// Deletes itself when dismissed or when its lifetime runs out.
class NotificationDialog : public QDialog
{
    Q_OBJECT

public:
    explicit NotificationDialog(QWidget *parent = nullptr);
    ~NotificationDialog();

    /// Show this message (title, text, priority colour, lifetime).
    void setMessage(const ModuleMsg &msg);

    void setTitle(QString title);
    void setMsg(QString msg);
    void setPriority(MsgPriority pri);
    /// Close after this many seconds; negative = until dismissed.
    void setLifetime(double seconds);

private:
    MsgPriority priority;
    Ui::NotificationDialog *ui;
    QTimer lifetimeTimer;

    void changeColor();
};

#endif // NOTIFICATIONDIALOG_H
