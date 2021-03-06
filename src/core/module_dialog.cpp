#include "module_dialog.h"
#include "interfaces.h"

#include <QDir>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPluginLoader>
#include <QPushButton>
#include <QStringList>
#include <QTreeWidget>
#include <QTreeWidgetItem>

ModuleDialog::ModuleDialog(const QString &path, const QStringList &fileNames,
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

    setWindowTitle(tr("Plugin Information"));
    findPlugins(path, fileNames);
}

//! [0]
void ModuleDialog::findPlugins(const QString &path,
                               const QStringList &fileNames)
{
    label->setText(tr("Found the following modules\n"
                      "(looked in %1):")
                   .arg(QDir::toNativeSeparators(path)));

    const QDir dir(path);

    const auto staticInstances = QPluginLoader::staticInstances();
    for (QObject *plugin : staticInstances)
        populateTreeWidget(plugin, tr("%1 (Static Plugin)")
                                   .arg(plugin->metaObject()->className()));

    for (const QString &fileName : fileNames) {
        QPluginLoader loader(dir.absoluteFilePath(fileName));
        QObject *plugin = loader.instance();
        if (plugin)
            populateTreeWidget(plugin, fileName);
    }
}
//! [0]

//! [1]
void ModuleDialog::populateTreeWidget(QObject *plugin, const QString &text)
{
    auto pluginItem = new QTreeWidgetItem(treeWidget);
    pluginItem->setText(0, text);
    pluginItem->setExpanded(true);

    QFont boldFont = pluginItem->font(0);
    boldFont.setBold(true);
    pluginItem->setFont(0, boldFont);

    if (plugin) {
        auto iImporter = qobject_cast<ImportArticlesInterface *>(plugin);
        if (iImporter)
            addItems(pluginItem, "ImportInterface", iImporter->FileTypes());
    }
}
//! [1]

void ModuleDialog::addItems(QTreeWidgetItem *pluginItem,
                            const char *interfaceName,
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
