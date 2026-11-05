// A module left over from an older API. Its metadata says so, and that must be
// enough: the core skips it without loading the DLL, so this code never runs.
#include <interfaces.h>

namespace {
struct LeavesATrace {
    LeavesATrace() { qputenv("AGUARA_OLD_MODULE_RAN", "1"); }
} trace;
}

class OldApiModule : public QObject, public AguaraModuleInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.AguaraPOS.ModuleInterface/0")
    Q_INTERFACES(AguaraModuleInterface)

public:
    ModuleInfo moduleInfo() const override { return ModuleInfo(); }
};

#include "old_api.moc"
