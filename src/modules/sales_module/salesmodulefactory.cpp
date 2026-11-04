#include "salesmodulefactory.h"
#include "salesmodule.h"

#include <version.h>

SalesModuleFactory::SalesModuleFactory(QObject *parent) : QObject(parent)
{
}

ModuleInfo SalesModuleFactory::moduleInfo() const
{
    ModuleInfo info;
    info.id          = "sales";
    info.name        = "Ventas";
    info.description = "Cobro, sesión de caja, arqueo y cierre";
    info.version     = VERSION_STRING;
    info.iconName    = "cart";      // theme icon (AguaraStyle::icon)
    return info;
}

bool SalesModuleFactory::initialize(AguaraHost *host)
{
    products = host->service<ProductService>();
    inventory = host->service<InventoryService>();
    users = host->service<UserService>();
    cash = host->service<CashService>();
    sales = host->service<SalesService>();
    print = host->service<PrintService>();
    return products && inventory && users && cash && sales && print;
}

QList<PermissionInfo> SalesModuleFactory::permissions() const
{
    const QString sale = QStringLiteral("Ventas");
    const QString till = QStringLiteral("Caja");
    return {
        { modulePermission(QStringLiteral("sales")), QStringLiteral("Abrir Ventas"), QStringLiteral("Módulos"),
          true, true },     // cashiers too
        { QStringLiteral("ventas.anular"), QStringLiteral("Anular ventas"), sale },
        { QStringLiteral("ventas.descuento_libre"), QStringLiteral("Autorizar descuentos sobre el límite del rol"), sale },
        { QStringLiteral("ventas.comprobante_repetido"), QStringLiteral("Aceptar un comprobante ya usado en otra venta"), sale },
        { QStringLiteral("ventas.reimprimir"), QStringLiteral("Reimprimir tickets (salen con la marca COPIA)"), sale },
        { QStringLiteral("caja.abrir"), QStringLiteral("Abrir la caja"), till, true, true },
        { QStringLiteral("caja.cerrar"), QStringLiteral("Cerrar la caja con arqueo"), till, true, true },
        { QStringLiteral("caja.movimientos"), QStringLiteral("Registrar ingresos, egresos y retiros"), till },
        { QStringLiteral("caja.abrir_cajon"), QStringLiteral("Abrir el cajón sin una venta"), till },
        { QStringLiteral("caja.cerrar_diferencia"), QStringLiteral("Autorizar el cierre con diferencia"), till },
        { QStringLiteral("caja.cerrar_olvidada"), QStringLiteral("Cerrar una caja de otro día"), till },
        { QStringLiteral("caja.planillas"), QStringLiteral("Ver planillas de cierre anteriores"), till },
        { QStringLiteral("caja.configurar"), QStringLiteral("Cambiar el nombre de la caja"), till },
    };
}

/// While a session is open, even a forgotten one, this page is the main window.
bool SalesModuleFactory::keepPageOpen() const
{
    return cash->currentSession().has_value();
}

QWidget *SalesModuleFactory::createWidget(QWidget *parent)
{
    return new SalesModule(products, inventory, users, cash, sales, print, parent);
}
