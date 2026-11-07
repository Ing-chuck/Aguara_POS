#ifndef IMPORT_MODULE_H
#define IMPORT_MODULE_H

#include <interfaces.h>

#include <QObject>

class ImportModule : public QObject,
                     public AguaraModuleInterface,
                     public ImportInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID AguaraModuleInterface_iid FILE "import_module.json")
    Q_INTERFACES(AguaraModuleInterface ImportInterface)

public:
    explicit ImportModule(QObject *parent = nullptr);

    // AguaraModuleInterface
    ModuleInfo moduleInfo() const override;
    bool initialize(AguaraHost *host) override;

    // ImportInterface
    QStringList fileTypes() const override;
    bool canImport(ImportKind kind) const override;
    bool importFrom(const QString &fileName, ImportKind kind) override;

private:
    AguaraHost *host = nullptr;
    ProductService *products = nullptr;
    CustomerService *customers = nullptr;
    InventoryService *inventory = nullptr;

    bool importArticles(const QString &fileName);
    bool importCustomers(const QString &fileName);
    bool importSuppliers(const QString &fileName);

    bool readFile(const QString &fileName, QString *text);
    bool failed(const QString &text);
    void finish(QString summary, const QStringList &problems);
    void report(const QString &title, const QString &text, MsgPriority priority);
};

#endif // IMPORT_MODULE_H
