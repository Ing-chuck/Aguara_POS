#include "module_dialog.h"

#include <QDir>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QStringList>
#include <QTreeWidget>
#include <QTreeWidgetItem>

ModuleDialog::ModuleDialog(const QString &path, const QList<LoadedModule> &modules,
                           QWidget *parent) :
    QDialog(parent),
    label(new QLabel),
    treeWidget(new QTreeWidget),
    okButton(new QPushButton(tr("OK")))
{
    treeWidget->setAlternatingRowColors(false);
    treeWidget->setSelectionMode(QAbstractItemView::NoSelection);
    treeWidget->setColumnCount(1);
    treeWidget->header()->hide();

    okButton->setDefault(true);

    connect(okButton, &QAbstractButton::clicked, this, &QWidget::close);

    QGridLayout *mainLayout = new QGridLayout;
    mainLayout->setColumnStretch(0, 1);
    mainLayout->setColumnStretch(2, 1);
    mainLayout->addWidget(label, 0, 0, 1, 3);
    mainLayout->addWidget(treeWidget, 1, 0, 1, 3);
    mainLayout->addWidget(okButton, 2, 1);
    setLayout(mainLayout);

    interfaceIcon.addPixmap(style()->standardPixmap(QStyle::SP_DirOpenIcon),
                            QIcon::Normal, QIcon::On);
    interfaceIcon.addPixmap(style()->standardPixmap(QStyle::SP_DirClosedIcon),
                            QIcon::Normal, QIcon::Off);
    featureIcon.addPixmap(style()->standardPixmap(QStyle::SP_FileIcon));

    setWindowTitle(tr("Module Information"));

    label->setText(tr("Loaded the following modules\n"
                      "(from %1):")
                   .arg(QDir::toNativeSeparators(path)));

    for (const LoadedModule &module : modules)
        populateTreeWidget(module);
}

void ModuleDialog::populateTreeWidget(const LoadedModule &module)
{
    auto pluginItem = new QTreeWidgetItem(treeWidget);
    pluginItem->setText(0, QString("%1 %2  (%3)").arg(module.info.name,
                                                      module.info.version,
                                                      module.fileName));
    pluginItem->setIcon(0, module.info.icon);
    pluginItem->setToolTip(0, module.info.description);
    pluginItem->setExpanded(true);

    QFont boldFont = pluginItem->font(0);
    boldFont.setBold(true);
    pluginItem->setFont(0, boldFont);

    if (module.widgets) {
        addItems(pluginItem, tr("Page"),
                 { module.widgets->allowsMultipleInstances() ? tr("multiple instances")
                                                             : tr("single instance") });
    }
    if (module.importer)
        addItems(pluginItem, tr("Import"), module.importer->fileTypes());
}

void ModuleDialog::addItems(QTreeWidgetItem *pluginItem,
                            const QString &interfaceName,
                            const QStringList &features)
{
    auto interfaceItem = new QTreeWidgetItem(pluginItem);
    interfaceItem->setText(0, interfaceName);
    interfaceItem->setIcon(0, interfaceIcon);

    for (QString feature : features) {
        if (feature.endsWith("..."))
            feature.chop(3);
        auto featureItem = new QTreeWidgetItem(interfaceItem);
        featureItem->setText(0, feature);
        featureItem->setIcon(0, featureIcon);
    }
}
