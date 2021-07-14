#ifndef IMPORT_MODULE_H
#define IMPORT_MODULE_H

#include <interfaces.h>

#include <QObject>
#include <QtPlugin>

class ImportModule : public QObject, public ImportArticlesInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.AguaraPOS.importArticlesInterface" FILE "import_module.json")
    Q_INTERFACES(ImportArticlesInterface)

public:
    bool registerModule(QString version) const override;
    QStringList FileTypes() const override;
    bool ImportFrom(QString filename) override;

//signals:

private:
    void insertInto(QStringList fields, QList<QVariant> data);

};

#endif // IMPORT_MODULE_H
