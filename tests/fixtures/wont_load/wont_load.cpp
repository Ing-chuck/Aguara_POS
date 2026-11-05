// A module of the right API that Windows refuses to load, like one with a DLL
// missing next to it: the metadata reads fine, loading it fails.
#include <interfaces.h>

#include <windows.h>

class WontLoadModule : public QObject, public AguaraModuleInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID AguaraModuleInterface_iid)
    Q_INTERFACES(AguaraModuleInterface)

public:
    ModuleInfo moduleInfo() const override { return ModuleInfo(); }
};

extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return FALSE; }

#include "wont_load.moc"
