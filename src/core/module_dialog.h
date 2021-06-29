#ifndef MODULE_DIALOG_H
#define MODULE_DIALOG_H

#include <QDialog>
#include <QIcon>

QT_BEGIN_NAMESPACE
class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;
QT_END_NAMESPACE

class ModuleDialog : public QDialog
{
    Q_OBJECT

public:
    ModuleDialog(const QString &path, const QStringList &fileNames,
                 QWidget *parent = nullptr);

private:
    void findPlugins(const QString &path, const QStringList &fileNames);
    void populateTreeWidget(QObject *plugin, const QString &text);
    void addItems(QTreeWidgetItem *pluginItem, const char *interfaceName,
                  const QStringList &features);

    QLabel *label = nullptr;
    QTreeWidget *treeWidget = nullptr;
    QPushButton *okButton = nullptr;
    QIcon interfaceIcon;
    QIcon featureIcon;
};

#endif // MODULE_DIALOG_H
