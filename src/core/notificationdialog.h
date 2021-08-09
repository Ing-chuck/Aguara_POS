#ifndef NOTIFICATIONDIALOG_H
#define NOTIFICATIONDIALOG_H

#include "messages.h"

#include <QDialog>

namespace Ui {
class NotificationDialog;
}

class NotificationDialog : public QDialog
{
    Q_OBJECT

public:
    explicit NotificationDialog(QWidget *parent = nullptr);
    ~NotificationDialog();

    void setTitle(QString title);
    void setMsg(QString msg);
    void setPriority(MsgPriority pri);

private:
    MsgPriority priority;
    Ui::NotificationDialog *ui;

    void changeColor();
};

#endif // NOTIFICATIONDIALOG_H
