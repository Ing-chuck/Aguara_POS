#include "reportsfactory.h"
#include "reportspage.h"

#include <version.h>

ReportsFactory::ReportsFactory(QObject *parent) : QObject(parent)
{
}

ModuleInfo ReportsFactory::moduleInfo() const
{
    ModuleInfo info;
    info.id          = "reports";
    info.name        = "Reportes";
    info.description = "Ventas, caja, inventario, IVA y auditoría";
    info.version     = VERSION_STRING;
    info.iconName    = "list";      // theme icon (AguaraStyle::icon)
    return info;
}

bool ReportsFactory::initialize(AguaraHost *host)
{
    reports = host->service<ReportService>();
    users = host->service<UserService>();
    sales = host->service<SalesService>();
    return reports && users && sales;
}

QList<PermissionInfo> ReportsFactory::permissions() const
{
    const QString group = QStringLiteral("Reportes");
    return {
        { QStringLiteral("reportes.costos"), QStringLiteral("Ver costos y márgenes"), group },
        { QStringLiteral("reportes.auditoria"), QStringLiteral("Ver la auditoría"), group },
    };
}

QWidget *ReportsFactory::createWidget(QWidget *parent)
{
    return new ReportsPage(reports, users, sales, parent);
}
