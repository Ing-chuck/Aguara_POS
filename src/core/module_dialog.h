#ifndef MODULE_DIALOG_H
#define MODULE_DIALOG_H

#include "loadedmodule.h"

#include <QDialog>
#include <QIcon>

QT_BEGIN_NAMESPACE
class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;
QT_END_NAMESPACE

/// "About modules": lists the modules the core loaded and what each provides.
class ModuleDialog : public QDialog
{
    Q_OBJECT

public:
    ModuleDialog(const QString &path, const QList<LoadedModule> &modules,
                 QWidget *parent = nullptr);

private:
    void populateTreeWidget(const LoadedModule &module);
    void addItems(QTreeWidgetItem *pluginItem, const QString &interfaceName,
                  const QStringList &features);

    QLabel *label = nullptr;
    QTreeWidget *treeWidget = nullptr;
    QPushButton *okButton = nullptr;
    QIcon interfaceIcon;
    QIcon featureIcon;
};

#endif // MODULE_DIALOG_H
