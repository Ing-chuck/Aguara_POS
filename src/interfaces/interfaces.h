#ifndef INTERFACES_H
#define INTERFACES_H

#include "aguara_global.h"
#include "aguarahost.h"
#include "aguarastyle.h"
#include "barcode.h"
#include "cashservice.h"
#include "inventoryservice.h"
#include "messages.h"
#include "money.h"
#include "printservice.h"
#include "productservice.h"
#include "salesservice.h"
#include "userservice.h"

#include <QIcon>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <QtPlugin>

/*
    How an Aguara module is put together
    ------------------------------------
    A module is a Qt plugin whose root object is a plain QObject that
    implements AguaraModuleInterface (who am I) plus one or more capability
    interfaces (what can I do):

        class MyModule : public QObject,
                         public AguaraModuleInterface,
                         public WidgetModuleInterface
        {
            Q_OBJECT
            Q_PLUGIN_METADATA(IID AguaraModuleInterface_iid FILE "module.json")
            Q_INTERFACES(AguaraModuleInterface WidgetModuleInterface)
            ...
        };

    The root object is created once when the core loads the plugin, and is
    only a descriptor/factory. The widgets it creates are separate objects,
    owned by the core once handed over.

    Right after loading, the core calls initialize() with the host, where the
    module fetches the services it needs (see aguarahost.h). Data lives in
    the core: modules use services such as ProductService, never SQL.

    Compatibility: each interface ID ends in a version. When an interface
    changes in an incompatible way, bump its version. Modules built against
    the old version then no longer match, and the core skips them instead of
    crashing.
*/

/// Static description of a module: everything the core needs to list it
/// and show it to the user, without creating any of its widgets.
struct ModuleInfo {
    QString id;           ///< stable, unique, machine-readable (e.g. "inventory")
    QString name;         ///< user-visible name
    QString description;  ///< one line shown in "About modules"
    QString version;      ///< the module's own version, for display
    QIcon   icon;         ///< used when iconName is empty
    /// A theme icon (AguaraStyle::icon(), e.g. "box"). Preferred over `icon`:
    /// the core draws it in the right color for each place (dark sidebar,
    /// light tabs and dialogs).
    QString iconName;
};

/// Implemented by every module. The core rejects plugins that don't.
class AguaraModuleInterface {
public:
    virtual ~AguaraModuleInterface() = default;
    virtual ModuleInfo moduleInfo() const = 0;

    /// Called once, before any other capability method. Fetch services here.
    /// Return false if something the module needs is missing: the core then
    /// skips the module and tells the user.
    virtual bool initialize(AguaraHost *host) { Q_UNUSED(host) return true; }
};

/// Capability: the module provides a page that the core shows as a tab.
class WidgetModuleInterface {
public:
    virtual ~WidgetModuleInterface() = default;

    /// Create a new page. The caller takes ownership.
    /// If the page derives from AguaraWidget, its notify() signal is
    /// connected to the core's notification area.
    virtual QWidget *createWidget(QWidget *parent = nullptr) = 0;

    /// false: the core keeps at most one page open and re-focuses it.
    /// true: every click on the module's button opens a new page.
    virtual bool allowsMultipleInstances() const { return false; }

    /// true: right now the module's page has to stay open. The core opens it
    /// as soon as someone who may use it signs in, puts it first and doesn't
    /// let it be closed, and it survives a change of user: the page must not
    /// depend on who was signed in when it was built. A page that derives from
    /// AguaraWidget emits keepOpenChanged() when the answer changes.
    virtual bool keepPageOpen() const { return false; }
};

/// Capability: the module can import articles from files.
class ImportArticlesInterface {
public:
    virtual ~ImportArticlesInterface() = default;

    /// File extensions this importer handles, without the dot (e.g. "csv").
    virtual QStringList fileTypes() const = 0;
    virtual bool importFrom(const QString &fileName) = 0;
};

/// Capability: the module has permissions of its own. They appear in the
/// roles screen; check them with UserService::can() / authorize().
class PermissionProvider {
public:
    virtual ~PermissionProvider() = default;
    virtual QList<PermissionInfo> permissions() const = 0;
};

/// Optional base class for module pages that send notifications to the user.
class AGUARA_EXPORT AguaraWidget : public QWidget {
    Q_OBJECT
public:
    explicit AguaraWidget(QWidget *parent = nullptr);
    ~AguaraWidget() override;

signals:
    /// Signals emitted from the constructor are lost: nothing is connected
    /// yet. Defer them, e.g. QTimer::singleShot(0, this, ...).
    void notify(const ModuleMsg &msg);

    /// WidgetModuleInterface::keepPageOpen() now answers something else.
    void keepOpenChanged();
};

// 1.1: ModuleInfo::iconName. 1.2: WidgetModuleInterface::keepPageOpen()
#define AguaraModuleInterface_iid    "org.AguaraPOS.ModuleInterface/1.2"
#define WidgetModuleInterface_iid    "org.AguaraPOS.WidgetModuleInterface/1.1"
#define ImportArticlesInterface_iid  "org.AguaraPOS.ImportArticlesInterface/1.0"
#define PermissionProvider_iid       "org.AguaraPOS.PermissionProvider/1.0"

Q_DECLARE_INTERFACE(AguaraModuleInterface,   AguaraModuleInterface_iid)
Q_DECLARE_INTERFACE(WidgetModuleInterface,   WidgetModuleInterface_iid)
Q_DECLARE_INTERFACE(ImportArticlesInterface, ImportArticlesInterface_iid)
Q_DECLARE_INTERFACE(PermissionProvider,      PermissionProvider_iid)

#endif // INTERFACES_H
