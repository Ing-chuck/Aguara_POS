#include "pagecero.h"
#include "ui_pagecero.h"

#include <QScreen>

PageCero::PageCero(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::PageCero)
{
    ui->setupUi(this);

    QScreen *screen = this->window()->screen();
    w = screen->availableGeometry().width()/4;
    h = screen->availableGeometry().height()/5;
}

void PageCero::addWidget(QWidget *widget) {
    widget->setMinimumSize(w, h);
    int row = count / cols;
    int col = count % cols;
    ui->gridLayoutMain->addWidget(widget, row, col);
    count += 1;
}
