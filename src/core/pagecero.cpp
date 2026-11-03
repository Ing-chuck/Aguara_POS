#include "pagecero.h"
#include "ui_pagecero.h"

#include <aguarastyle.h>

#include <QTimer>

PageCero::PageCero(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::PageCero)
{
    ui->setupUi(this);
    ui->emptyIcon->setPixmap(AguaraStyle::icon(QStringLiteral("bell"),
        AguaraStyle::color(AguaraStyle::Color::BorderStrong)).pixmap(48, 48));
    ui->dismissAllButton->setIcon(AguaraStyle::icon(QStringLiteral("close")));
    connect(ui->dismissAllButton, &QPushButton::clicked, this, &PageCero::dismissAll);
    refresh();
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
    items.prepend(widget);
    ui->cardsLayout->insertWidget(0, widget);     // newest on top
    // close the gap once the card is fully gone
    connect(widget, &QObject::destroyed, this, [this]() {
        QTimer::singleShot(0, this, &PageCero::refresh);
    });
    refresh();
}

int PageCero::count() const
{
    int n = 0;
    for (const QPointer<QWidget> &item : items)
        n += item ? 1 : 0;
    return n;
}

void PageCero::dismissAll()
{
    for (const QPointer<QWidget> &item : qAsConst(items)) {
        if (item)
            item->close();      // cards delete themselves on close
    }
}

void PageCero::refresh() {
    items.removeAll(QPointer<QWidget>());
    const bool empty = items.isEmpty();
    ui->emptyState->setVisible(empty);
    ui->dismissAllButton->setEnabled(!empty);
    ui->subtitle->setText(empty ? tr("Mensajes del sistema y de los módulos")
                          : items.size() == 1 ? tr("1 aviso")
                                              : tr("%1 avisos").arg(items.size()));
    emit countChanged(items.size());
}
