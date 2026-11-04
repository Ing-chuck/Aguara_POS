#ifndef SESSIONDIALOGS_H
#define SESSIONDIALOGS_H

#include <QDialog>

class SqlUserService;
class UserService;

QT_BEGIN_NAMESPACE
class QLabel;
class QLineEdit;
QT_END_NAMESPACE

/// Sign in. Modes: at startup, on a locked session (the same user unlocks,
/// or someone else takes over), and switching user. Accepted means somebody
/// is signed in. In the locked modes it can't be dismissed: the only way out
/// is "Salir", which closes the program.
class LoginDialog : public QDialog
{
    Q_OBJECT
public:
    enum Mode { Start, Locked, Switch };
    LoginDialog(SqlUserService *users, Mode mode, QWidget *parent = nullptr);

    /// Locked: the program can't be left from here, and `reason` says why.
    /// Whoever locked is still signed in, and nobody has proved to be them.
    void forbidExit(const QString &reason);

    void accept() override;
    void reject() override;

private:
    SqlUserService *users;
    Mode mode;
    bool exitForbidden = false;
    QLineEdit *username;
    QLineEdit *password;
    QLabel *error;
};

/// First run: the database has no users yet, so create the administrator.
class FirstAdminDialog : public QDialog
{
    Q_OBJECT
public:
    explicit FirstAdminDialog(SqlUserService *users, QWidget *parent = nullptr);
    void accept() override;

private:
    SqlUserService *users;
    QLineEdit *fullName;
    QLineEdit *username;
    QLineEdit *password;
    QLineEdit *repeat;
    QLabel *error;
};

/// Change your own password. `forced`: an administrator set it, so it must
/// be replaced before going on.
class ChangePasswordDialog : public QDialog
{
    Q_OBJECT
public:
    ChangePasswordDialog(UserService *users, bool forced, QWidget *parent = nullptr);
    void accept() override;

private:
    UserService *users;
    QLineEdit *current;
    QLineEdit *replacement;
    QLineEdit *repeat;
    QLabel *error;
};

/// The supervisor prompt used by UserService::authorize().
bool askSupervisor(const QString &permissionDescription, const QString &reason, const QString &errorText,
                   QWidget *parent, QString *username, QString *password);

#endif // SESSIONDIALOGS_H
