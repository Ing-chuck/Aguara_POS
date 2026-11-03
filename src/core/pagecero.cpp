#include "pagecero.h"
#include "ui_pagecero.h"

#include <QScreen>
#include <QTimer>

PageCero::PageCero(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::PageCero)
{
    ui->setupUi(this);

    QScreen *screen = this->window()->screen();
    w = screen->availableGeometry().width()/4;
    h = screen->availableGeometry().height()/5;
}

PageCero::~PageCero()
{
    // the cards are deleted after this destructor (as child widgets): stop
    // listening first so they don't call back into a half-destroyed page
    for (const QPointer<QWidget> &item : qAsConst(items)) {
        if (item)
            disconnect(item, nullptr, this, nullptr);
    }
    delete ui;
}

void PageCero::addWidget(QWidget *widget) {
    widget->setMinimumSize(w, h);
    items.append(widget);
    // re-pack once the card is fully gone
    connect(widget, &QObject::destroyed, this, [this]() {
        QTimer::singleShot(0, this, &PageCero::relayout);
    });
    relayout();
}

/// Place the live cards row by row, without gaps
void PageCero::relayout() {
    items.removeAll(QPointer<QWidget>());
    for (const QPointer<QWidget> &item : qAsConst(items))
        ui->gridLayoutMain->removeWidget(item);
    for (int i = 0; i < items.size(); ++i)
        ui->gridLayoutMain->addWidget(items.at(i), i / cols, i % cols);
}
