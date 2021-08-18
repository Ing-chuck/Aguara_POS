#ifndef IMPORT_MODULE_H
#define IMPORT_MODULE_H

#include <interfaces.h>

#include <QObject>
#include <QtPlugin>

class ImportModule : public ImportArticlesInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.AguaraPOS.importArticlesInterface" FILE "import_module.json")
    Q_INTERFACES(ImportArticlesInterface)

public:
    ImportModule(QObject *parent = nullptr);
    QStringList FileTypes() const override;
    bool ImportFrom(QString filename) override;

private:
    QString compiledVersion() const override;
    void insertInto(QStringList fields, QList<QVariant> data);

};

#endif // IMPORT_MODULE_H
