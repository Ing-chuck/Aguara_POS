#ifndef USERSPAGE_H
#define USERSPAGE_H

#include <interfaces.h>

QT_BEGIN_NAMESPACE
class QDateEdit;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTableWidget;
class QTreeWidget;
QT_END_NAMESPACE

/// Three tabs: Usuarios, Roles (with usuarios.administrar) and Auditoría
/// (with auditoria.ver).
class UsersPage : public AguaraWidget
{
    Q_OBJECT
public:
    explicit UsersPage(UserService *users, QWidget *parent = nullptr);

private slots:
    void reloadUsers();
    void onNewUser();
    void onEditUser();

    void reloadRoles();
    void showRole();
    void onNewRole();
    void onSaveRole();
    void onRemoveRole();

    void reloadAudit();

private:
    QWidget *buildUsersTab();
    QWidget *buildRolesTab();
    QWidget *buildAuditTab();
    void editUser(const UserAccount &user);

    UserService *users;

    QTableWidget *usersTable = nullptr;
    QList<UserAccount> userList;
    QPushButton *editUserButton = nullptr;

    QListWidget *rolesList = nullptr;
    QList<UserRole> roleList;
    QLineEdit *roleName = nullptr;
    QLineEdit *roleDescription = nullptr;
    class QSpinBox *roleMaxDiscount = nullptr;
    QTreeWidget *rolePermissions = nullptr;
    QPushButton *saveRoleButton = nullptr;
    QPushButton *removeRoleButton = nullptr;

    QTableWidget *auditTable = nullptr;
    QDateEdit *auditFrom = nullptr;
    QDateEdit *auditTo = nullptr;
    QLineEdit *auditText = nullptr;
};

#endif // USERSPAGE_H
