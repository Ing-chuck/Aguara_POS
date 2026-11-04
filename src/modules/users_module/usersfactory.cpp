#include "usersfactory.h"
#include "userspage.h"

#include <version.h>

UsersFactory::UsersFactory(QObject *parent) : QObject(parent)
{
}

ModuleInfo UsersFactory::moduleInfo() const
{
    ModuleInfo info;
    info.id          = "users";
    info.name        = "Usuarios";
    info.description = "Usuarios, roles, permisos y registro de auditoría";
    info.version     = VERSION_STRING;
    info.iconName    = "users";
    return info;
}

bool UsersFactory::initialize(AguaraHost *host)
{
    users = host->service<UserService>();
    return users != nullptr;
}

QList<PermissionInfo> UsersFactory::permissions() const
{
    const QString group = QStringLiteral("Sistema");
    return {
        { modulePermission(QStringLiteral("users")), QStringLiteral("Abrir Usuarios"), QStringLiteral("Módulos") },
        { QStringLiteral("usuarios.administrar"), QStringLiteral("Administrar usuarios y roles"), group, false, false },
        { QStringLiteral("auditoria.ver"), QStringLiteral("Ver el registro de auditoría"), group },
    };
}

QWidget *UsersFactory::createWidget(QWidget *parent)
{
    return new UsersPage(users, parent);
}
