#include "module_dialog.h"

#include <aguarastyle.h>

#include <QDialogButtonBox>
#include <QDir>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

using AguaraStyle::Color;

ModuleDialog::ModuleDialog(const QString &path, const QList<LoadedModule> &modules,
                           QWidget *parent) :
    QDialog(parent)
{
    setWindowTitle(tr("Módulos instalados"));

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 18);
    layout->setSpacing(12);

    auto title = new QLabel(tr("Módulos instalados"));
    AguaraStyle::setRole(title, AguaraStyle::Title);
    auto subtitle = new QLabel(modules.isEmpty()
        ? tr("No se cargó ningún módulo.")
        : modules.size() == 1 ? tr("1 módulo cargado desde %1").arg(QDir::toNativeSeparators(path))
                              : tr("%1 módulos cargados desde %2").arg(modules.size()).arg(QDir::toNativeSeparators(path)));
    subtitle->setWordWrap(true);
    subtitle->setTextInteractionFlags(Qt::TextSelectableByMouse);
    AguaraStyle::setRole(subtitle, AguaraStyle::Subtitle);
    layout->addWidget(title);
    layout->addWidget(subtitle);

    auto list = new QWidget;
    auto listLayout = new QVBoxLayout(list);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(8);
    for (const LoadedModule &module : modules)
        listLayout->addWidget(moduleCard(module));
    listLayout->addStretch();

    auto scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setWidget(list);
    scroll->setFrameShape(QFrame::NoFrame);
    layout->addWidget(scroll, 1);

    auto buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->button(QDialogButtonBox::Close)->setText(tr("Cerrar"));
    buttons->button(QDialogButtonBox::Close)->setDefault(true);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    resize(620, qMin(160 + 96 * modules.size(), 560));
}

QWidget *ModuleDialog::moduleCard(const LoadedModule &module) const
{
    auto card = new QFrame;
    AguaraStyle::setRole(card, AguaraStyle::Card);
    auto row = new QHBoxLayout(card);
    row->setContentsMargins(14, 12, 14, 12);
    row->setSpacing(14);

    auto icon = new QLabel;
    QIcon i = module.info.iconName.isEmpty() ? module.info.icon
                                             : AguaraStyle::icon(module.info.iconName, AguaraStyle::color(Color::Accent));
    icon->setPixmap(i.pixmap(28, 28));
    icon->setFixedSize(36, 36);
    icon->setAlignment(Qt::AlignCenter);
    icon->setStyleSheet(QStringLiteral("background: %1; border-radius: 8px;").arg(AguaraStyle::color(Color::AccentSoft).name()));
    row->addWidget(icon, 0, Qt::AlignTop);

    auto text = new QVBoxLayout;
    text->setSpacing(3);
    auto name = new QLabel(QStringLiteral("<b>%1</b>&nbsp;&nbsp;<span style='color:%2'>%3</span>")
                               .arg(module.info.name.toHtmlEscaped(), AguaraStyle::color(Color::TextMuted).name(),
                                    module.info.version.toHtmlEscaped()));
    text->addWidget(name);
    if (!module.info.description.isEmpty()) {
        auto description = new QLabel(module.info.description);
        description->setWordWrap(true);
        text->addWidget(description);
    }

    QStringList provides;
    if (module.widgets)
        provides << (module.widgets->allowsMultipleInstances() ? tr("Página (varias a la vez)") : tr("Página"));
    if (module.importer)
        provides << tr("Importa %1").arg(module.importer->fileTypes().join(QStringLiteral(", ")).toUpper());
    auto details = new QLabel(QStringLiteral("%1  ·  %2").arg(provides.isEmpty() ? tr("Servicio") : provides.join(QStringLiteral("  ·  ")),
                                                             module.fileName));
    AguaraStyle::setRole(details, AguaraStyle::Muted);
    details->setTextInteractionFlags(Qt::TextSelectableByMouse);
    text->addWidget(details);
    row->addLayout(text, 1);
    return card;
}
