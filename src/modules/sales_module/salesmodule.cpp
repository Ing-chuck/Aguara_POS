#include "salesmodule.h"
#include "ui_salesmodule.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

/*
**** message notification syntax ****

     ModuleMsg msg(QString(), "id", "title", "text", [priority = normal], [lifetime in seconds = -1]);
     emit notify(msg);      // the core fills in the sender (this module's name)
*/

SalesModule::SalesModule(QWidget *parent)
    : AguaraWidget(parent)
    , ui(new Ui::SalesModule)
{
    ui->setupUi(this);

    // Placeholder until the checkout screen exists: a page header and an
    // empty state, in the same layout every page uses.
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto header = new QWidget;
    AguaraStyle::setRole(header, AguaraStyle::PageHeader);
    header->setAttribute(Qt::WA_StyledBackground);
    auto headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(24, 16, 24, 16);
    headerLayout->setSpacing(2);
    auto title = new QLabel(tr("Ventas"));
    AguaraStyle::setRole(title, AguaraStyle::Title);
    auto subtitle = new QLabel(tr("Cobro y registro de ventas"));
    AguaraStyle::setRole(subtitle, AguaraStyle::Subtitle);
    headerLayout->addWidget(title);
    headerLayout->addWidget(subtitle);
    layout->addWidget(header);

    auto empty = new QVBoxLayout;
    empty->setSpacing(6);
    empty->addStretch(2);
    auto icon = new QLabel;
    icon->setPixmap(AguaraStyle::icon(QStringLiteral("cart"),
        AguaraStyle::color(AguaraStyle::Color::BorderStrong)).pixmap(56, 56));
    empty->addWidget(icon, 0, Qt::AlignHCenter);
    auto emptyTitle = new QLabel(tr("La caja todavía no está disponible"));
    emptyTitle->setStyleSheet(QStringLiteral("font-size: 12pt; font-weight: 600;"));
    empty->addWidget(emptyTitle, 0, Qt::AlignHCenter);
    auto emptyText = new QLabel(tr("Este módulo está en desarrollo."));
    AguaraStyle::setRole(emptyText, AguaraStyle::Muted);
    empty->addWidget(emptyText, 0, Qt::AlignHCenter);
    empty->addStretch(3);
    layout->addLayout(empty, 1);
}

SalesModule::~SalesModule()
{
    delete ui;
}
