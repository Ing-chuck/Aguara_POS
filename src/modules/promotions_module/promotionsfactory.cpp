#include "promotionsfactory.h"
#include "promotionspage.h"

#include <version.h>

PromotionsFactory::PromotionsFactory(QObject *parent) : QObject(parent)
{
}

ModuleInfo PromotionsFactory::moduleInfo() const
{
    ModuleInfo info;
    info.id          = "promotions";
    info.name        = "Promociones";
    info.description = "Descuentos con fecha sobre los artículos elegidos";
    info.version     = VERSION_STRING;
    info.iconName    = "tag";       // theme icon (AguaraStyle::icon)
    return info;
}

bool PromotionsFactory::initialize(AguaraHost *host)
{
    promotions = host->service<PromotionService>();
    products = host->service<ProductService>();
    inventory = host->service<InventoryService>();
    users = host->service<UserService>();
    return promotions && products && inventory && users;
}

QList<PermissionInfo> PromotionsFactory::permissions() const
{
    return {
        { modulePermission(QStringLiteral("promotions")), QStringLiteral("Abrir Promociones"), QStringLiteral("Módulos") },
        { QStringLiteral("promociones.editar"), QStringLiteral("Crear, modificar y dar de baja promociones"),
          QStringLiteral("Promociones") },
    };
}

QWidget *PromotionsFactory::createWidget(QWidget *parent)
{
    return new PromotionsPage(promotions, products, inventory, users, parent);
}
