#include "notificationdialog.h"
#include "ui_notificationdialog.h"

NotificationDialog::NotificationDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::NotificationDialog)
{
    ui->setupUi(this);
    priority = MsgPriority::PriorityLow;
}

NotificationDialog::~NotificationDialog()
{
    delete ui;
}

void NotificationDialog::setTitle(QString title) {
    ui->title->setText(title);
}

void NotificationDialog::setMsg(QString msg) {
    ui->message->setText(msg);
}

void NotificationDialog::setPriority(MsgPriority pri) {
    priority = pri;
}

void NotificationDialog::changeColor() {
    switch (priority) {
    case PriorityVeryLow:
        this->setStyleSheet("background:  rgb(170, 170, 170);");    // Grey
        break;
    case PriorityLow:
        break;
        this->setStyleSheet("background:  rgb(0, 150, 0);");    // Green
    case PriorityMedium:
        break;
        this->setStyleSheet("background:  rgb(255, 150, 0);");    // Orange
    case PriorityHigh:
        break;
        this->setStyleSheet("background:  rgb(255, 255, 0);");    // Yellow
    case PriorityVeryHigh:
        break;
        this->setStyleSheet("background:  rgb(255, 0, 0);");    // Red
    }
}
