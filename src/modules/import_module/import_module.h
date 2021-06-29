#ifndef IMPORT_MODULE_H
#define IMPORT_MODULE_H

#include <interfaces.h>

#include <QObject>
#include <QtPlugin>

class import_module : public QObject, public importArticlesInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.AguaraPOS.importArticlesInterface" FILE "import_module.json")
    Q_INTERFACES(importArticlesInterface)

public:
   QStringList features() const override;
   bool importFrom(QString filename) override;

signals:

};

#endif // IMPORT_MODULE_H
