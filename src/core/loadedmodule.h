#ifndef LOADEDMODULE_H
#define LOADEDMODULE_H

#include "interfaces.h"

#include <QPointer>

/// The core's record of one successfully loaded module.
struct LoadedModule {
    QString     fileName;             ///< plugin file, relative to the modules folder
    QObject    *instance = nullptr;   ///< plugin root object (owned by Qt's plugin loader)
    ModuleInfo  info;

    // Capabilities the module implements (nullptr when it doesn't).
    WidgetModuleInterface   *widgets  = nullptr;
    ImportArticlesInterface *importer = nullptr;

    /// The open page of a single-instance widget module, if any.
    QPointer<QWidget> openPage;
};

#endif // LOADEDMODULE_H
