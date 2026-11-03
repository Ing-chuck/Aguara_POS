#ifndef IMPORT_MODULE_H
#define IMPORT_MODULE_H

#include <interfaces.h>

#include <QObject>

class ImportModule : public QObject,
                     public AguaraModuleInterface,
                     public ImportArticlesInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID AguaraModuleInterface_iid FILE "import_module.json")
    Q_INTERFACES(AguaraModuleInterface ImportArticlesInterface)

public:
    explicit ImportModule(QObject *parent = nullptr);

    // AguaraModuleInterface
    ModuleInfo moduleInfo() const override;
    bool initialize(AguaraHost *host) override;

    // ImportArticlesInterface
    QStringList fileTypes() const override;
    bool importFrom(const QString &fileName) override;

private:
    AguaraHost *host = nullptr;
    ProductService *products = nullptr;

    void report(const QString &title, const QString &text, MsgPriority priority);
};

#endif // IMPORT_MODULE_H
