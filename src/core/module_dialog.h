#ifndef MODULE_DIALOG_H
#define MODULE_DIALOG_H

#include "loadedmodule.h"

#include <QDialog>

QT_BEGIN_NAMESPACE
class QVBoxLayout;
QT_END_NAMESPACE

/// "Módulos instalados": one card per module the core loaded, with what it
/// provides and the file it came from.
class ModuleDialog : public QDialog
{
    Q_OBJECT

public:
    ModuleDialog(const QString &path, const QList<LoadedModule> &modules,
                 QWidget *parent = nullptr);

private:
    QWidget *moduleCard(const LoadedModule &module) const;
};

#endif // MODULE_DIALOG_H
