// Users, permissions, supervisor authorization and the audit log, on SQLite
// and PostgreSQL.
#include "inactivitywatcher.h"
#include "passwordhash.h"
#include "sqluserservice.h"
#include "testsupport.h"

#include <QSignalSpy>
#include <QtTest>

using TestSupport::CoreFixture;

class tst_Users : public QObject
{
    Q_OBJECT

private slots:
    void discountLimitComesFromTheRoles();
    void usernamesHaveNoSpaces();
    void lastAuthorizerNamesWhoAnswered();
    void initTestCase_data() { TestSupport::addBackendRows(); }
    void init() { PasswordHash::setDefaultIterations(200); }    // the rules, not the cost
    void cleanup();

    void passwordHashes();
    void oldHashesAreUpgradedAtLogin();
    void firstAdminOnlyOnce();
    void administratorCanEverything();
    void loginFailures_data();
    void loginFailures();
    void repeatedFailuresWait();
    void inactiveUserCantEnter();
    void namesIgnoreCase();
    void permissionsComeFromRoles();
    void newPermissionsGrantedOnlyOnce();
    void adminsPasswordMustBeReplaced();
    void changePasswordRules();
    void lastAdministratorIsProtected();
    void rolesRules();
    void onlyAdministratorsAdminister();
    void supervisorAuthorizes();
    void supervisorWithoutPermissionIsRefused();
    void cancelledAuthorization();
    void auditRecordsSessions();
    void auditRecordsProducts();
    void auditFilter();
    void inactivityWatcher();

private:
    bool open(QString *why);
    UserRole role(const QString &name) const;
    bool addUser(const QString &name, const QString &roleName, const QString &password = QStringLiteral("clave123"));
    /// Replace the password an administrator assigned, so the user can work.
    bool signIn(const QString &name, const QString &password = QStringLiteral("clave123"));

    QTemporaryDir m_tmp;
    CoreFixture m_core;
    std::unique_ptr<SqlUserService> m_users;
};

// Every test starts with an administrator signed in and the permissions a
// startup would register.
#define OPEN_USERS()                                                    \
    do {                                                                \
        AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());                      \
        m_users.reset(new SqlUserService(m_core.monitor.get()));        \
        m_core.products->setAuditLog(m_users.get());                    \
        QVERIFY2(m_users->createFirstAdmin("admin", "Administrador", "secreto1"), qPrintable(m_users->lastError())); \
        QVERIFY(m_users->registerPermissions({                          \
            { "modulo.inventory", "Abrir Inventario", "Módulos" },      \
            { "modulo.sales", "Abrir Ventas", "Módulos", true, true },  \
            { "inventario.baja", "Dar de baja", "Inventario" },         \
            { "usuarios.administrar", "Usuarios", "Sistema", false, false } })); \
    } while (0)

void tst_Users::cleanup()
{
    if (m_core.products)
        m_core.products->setAuditLog(nullptr);
    m_users.reset();
    m_core.close();
}

UserRole tst_Users::role(const QString &name) const
{
    for (const UserRole &r : m_users->roles()) {
        if (r.name == name)
            return r;
    }
    return UserRole();
}

bool tst_Users::addUser(const QString &name, const QString &roleName, const QString &password)
{
    UserAccount u;
    u.username = name;
    u.fullName = name.toUpper();
    u.roleIds = { role(roleName).id };
    return m_users->saveUser(u, password);
}

bool tst_Users::signIn(const QString &name, const QString &password)
{
    if (!m_users->login(name, password))
        return false;
    if (m_users->currentUser()->mustChangePassword)
        return m_users->changePassword(password, password + QStringLiteral("x"));
    return true;
}

void tst_Users::passwordHashes()
{
    PasswordHash::setDefaultIterations(120000);
    const QString a = PasswordHash::create(QStringLiteral("contraseña"));
    const QString b = PasswordHash::create(QStringLiteral("contraseña"));
    QVERIFY(a != b);                                        // a salt per hash
    QVERIFY(!a.contains(QStringLiteral("contraseña")));
    QVERIFY(a.startsWith(QStringLiteral("pbkdf2-sha256$")));
    QVERIFY(PasswordHash::verify(QStringLiteral("contraseña"), a));
    QVERIFY(!PasswordHash::verify(QStringLiteral("contrasena"), a));
    QVERIFY(!PasswordHash::verify(QStringLiteral("x"), QStringLiteral("garbage")));
    QVERIFY(!PasswordHash::needsUpgrade(a));
    QVERIFY(PasswordHash::needsUpgrade(PasswordHash::create(QStringLiteral("x"), 10)));
}

void tst_Users::oldHashesAreUpgradedAtLogin()
{
    OPEN_USERS();
    PasswordHash::setDefaultIterations(300);
    QVERIFY(m_users->login(QStringLiteral("admin"), QStringLiteral("secreto1")));
    QSqlQuery q(m_core.connection());
    QVERIFY(q.exec(QStringLiteral("SELECT clave FROM usuarios")) && q.next());
    QVERIFY2(q.value(0).toString().contains(QStringLiteral("$300$")), qPrintable(q.value(0).toString()));
    q.finish();
    QVERIFY(m_users->login(QStringLiteral("admin"), QStringLiteral("secreto1")));   // and it still works
}

void tst_Users::firstAdminOnlyOnce()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    m_users.reset(new SqlUserService(m_core.monitor.get()));
    QVERIFY(!m_users->hasUsers());
    QVERIFY(!m_users->createFirstAdmin(QStringLiteral("admin"), QStringLiteral("A"), QStringLiteral("corta")));
    QVERIFY(m_users->lastError().contains(QStringLiteral("6 caracteres")));
    QVERIFY(m_users->createFirstAdmin(QStringLiteral("admin"), QStringLiteral("A"), QStringLiteral("secreto1")));
    QVERIFY(m_users->hasUsers());
    QVERIFY(m_users->currentUser().has_value());            // signed in
    QVERIFY(!m_users->createFirstAdmin(QStringLiteral("otro"), QStringLiteral("B"), QStringLiteral("secreto1")));
}

void tst_Users::administratorCanEverything()
{
    OPEN_USERS();
    QVERIFY(m_users->can(QStringLiteral("usuarios.administrar")));
    QVERIFY(m_users->can(QStringLiteral("algo.que.todavia.no.existe")));
}

void tst_Users::loginFailures_data()
{
    QTest::addColumn<QString>("user");
    QTest::addColumn<QString>("password");
    QTest::newRow("wrong password") << "admin" << "mal";
    QTest::newRow("unknown user")   << "nadie" << "secreto1";
    QTest::newRow("empty")          << ""      << "";
}

void tst_Users::loginFailures()
{
    OPEN_USERS();
    QFETCH(QString, user);
    QFETCH(QString, password);
    m_users->logout();
    QVERIFY(!m_users->login(user, password));
    QVERIFY(!m_users->currentUser().has_value());
    QCOMPARE(m_users->lastError(), QString::fromUtf8("Usuario o contraseña incorrectos."));   // same text: no hints
}

void tst_Users::repeatedFailuresWait()
{
    OPEN_USERS();
    m_users->logout();
    for (int i = 0; i < 5; ++i)
        QVERIFY(!m_users->login(QStringLiteral("admin"), QStringLiteral("mal")));
    QVERIFY(!m_users->login(QStringLiteral("admin"), QStringLiteral("secreto1")));   // even the right one
    QVERIFY2(m_users->lastError().contains(QStringLiteral("Espere")), qPrintable(m_users->lastError()));
}

void tst_Users::inactiveUserCantEnter()
{
    OPEN_USERS();
    QVERIFY(addUser(QStringLiteral("ana"), QStringLiteral("Cajero")));
    UserAccount ana;
    for (const UserAccount &u : m_users->users()) {
        if (u.username == QLatin1String("ana"))
            ana = u;
    }
    ana.active = false;
    QVERIFY(m_users->saveUser(ana));
    QVERIFY(!m_users->login(QStringLiteral("ana"), QStringLiteral("clave123")));
    QVERIFY(m_users->lastError().contains(QStringLiteral("desactivado")));
}

void tst_Users::namesIgnoreCase()
{
    OPEN_USERS();
    QVERIFY(addUser(QStringLiteral("Ana"), QStringLiteral("Cajero")));
    QVERIFY(!addUser(QStringLiteral("ANA"), QStringLiteral("Cajero")));
    QVERIFY(m_users->lastError().contains(QStringLiteral("Ya existe")));
    QVERIFY(m_users->login(QStringLiteral("aNa"), QStringLiteral("clave123")));
}

void tst_Users::permissionsComeFromRoles()
{
    OPEN_USERS();
    QVERIFY(addUser(QStringLiteral("cajero"), QStringLiteral("Cajero")));
    QVERIFY(addUser(QStringLiteral("encargado"), QStringLiteral("Encargado")));

    QVERIFY(signIn(QStringLiteral("cajero")));
    QVERIFY(m_users->can(QStringLiteral("modulo.sales")));
    QVERIFY(!m_users->can(QStringLiteral("modulo.inventory")));     // "can't enter Inventario"
    QVERIFY(!m_users->can(QStringLiteral("inventario.baja")));

    QVERIFY(signIn(QStringLiteral("encargado")));
    QVERIFY(m_users->can(QStringLiteral("modulo.inventory")));
    QVERIFY(m_users->can(QStringLiteral("inventario.baja")));
    QVERIFY(!m_users->can(QStringLiteral("usuarios.administrar")));

    m_users->logout();
    QVERIFY(!m_users->can(QStringLiteral("modulo.sales")));
}

void tst_Users::newPermissionsGrantedOnlyOnce()
{
    OPEN_USERS();
    UserRole cashier = role(QStringLiteral("Cajero"));
    QVERIFY(cashier.permissions.contains(QStringLiteral("modulo.sales")));
    cashier.permissions.removeAll(QStringLiteral("modulo.sales"));
    QVERIFY(m_users->saveRole(cashier));

    // the next startup registers it again: the administrator's choice stands
    QVERIFY(m_users->registerPermissions({ { "modulo.sales", "Abrir Ventas (nuevo texto)", "Módulos", true, true } }));
    QVERIFY(!role(QStringLiteral("Cajero")).permissions.contains(QStringLiteral("modulo.sales")));
    bool renamed = false;
    for (const PermissionInfo &p : m_users->permissions())
        renamed = renamed || p.description == QLatin1String("Abrir Ventas (nuevo texto)");
    QVERIFY(renamed);
}

void tst_Users::adminsPasswordMustBeReplaced()
{
    OPEN_USERS();
    QVERIFY(addUser(QStringLiteral("ana"), QStringLiteral("Cajero")));
    QVERIFY(m_users->login(QStringLiteral("ana"), QStringLiteral("clave123")));
    QVERIFY(m_users->currentUser()->mustChangePassword);
    QVERIFY(m_users->changePassword(QStringLiteral("clave123"), QStringLiteral("propia123")));
    QVERIFY(!m_users->currentUser()->mustChangePassword);
    QVERIFY(m_users->login(QStringLiteral("ana"), QStringLiteral("propia123")));
    QVERIFY(!m_users->currentUser()->mustChangePassword);
    QVERIFY(!m_users->login(QStringLiteral("ana"), QStringLiteral("clave123")));
}

void tst_Users::changePasswordRules()
{
    OPEN_USERS();
    QVERIFY(!m_users->changePassword(QStringLiteral("mal"), QStringLiteral("nueva123")));
    QVERIFY(!m_users->changePassword(QStringLiteral("secreto1"), QStringLiteral("corta")));
    QVERIFY(!m_users->changePassword(QStringLiteral("secreto1"), QStringLiteral("secreto1")));
    QVERIFY(m_users->changePassword(QStringLiteral("secreto1"), QStringLiteral("nueva123")));
}

void tst_Users::lastAdministratorIsProtected()
{
    OPEN_USERS();
    UserAccount admin = *m_users->currentUser();
    admin.roleIds = { role(QStringLiteral("Cajero")).id };
    QVERIFY(!m_users->saveUser(admin));
    QVERIFY(m_users->lastError().contains(QStringLiteral("administrador activo")));
    QVERIFY(m_users->can(QStringLiteral("usuarios.administrar")));      // rolled back

    admin = *m_users->currentUser();
    admin.active = false;
    QVERIFY(!m_users->saveUser(admin));                                 // nor yourself
    QVERIFY(m_users->lastError().contains(QStringLiteral("propio usuario")));
}

void tst_Users::rolesRules()
{
    OPEN_USERS();
    QVERIFY(!m_users->removeRole(role(QStringLiteral("Cajero")).id));   // system role

    UserRole admin = role(QStringLiteral("Administrador"));
    admin.description = QStringLiteral("x");
    QVERIFY(!m_users->saveRole(admin));                                 // fixed

    UserRole extra;
    extra.name = QStringLiteral("Repositor");
    extra.permissions = QStringList{ QStringLiteral("modulo.inventory") };
    QVERIFY2(m_users->saveRole(extra), qPrintable(m_users->lastError()));
    QVERIFY(!m_users->saveRole(extra));                                 // the name is taken
    QVERIFY(addUser(QStringLiteral("rep"), QStringLiteral("Repositor")));
    QVERIFY(!m_users->removeRole(role(QStringLiteral("Repositor")).id));  // in use
    QVERIFY(m_users->lastError().contains(QStringLiteral("usuarios con el rol")));

    QVERIFY(signIn(QStringLiteral("rep")));
    QVERIFY(m_users->can(QStringLiteral("modulo.inventory")));
    QVERIFY(!m_users->can(QStringLiteral("modulo.sales")));
}

void tst_Users::onlyAdministratorsAdminister()
{
    OPEN_USERS();
    QVERIFY(addUser(QStringLiteral("encargado"), QStringLiteral("Encargado")));
    QVERIFY(signIn(QStringLiteral("encargado")));
    QVERIFY(!addUser(QStringLiteral("nuevo"), QStringLiteral("Cajero")));
    QVERIFY(m_users->lastError().contains(QStringLiteral("No tiene permiso")));
    QVERIFY(!m_users->saveRole(role(QStringLiteral("Cajero"))));
}

void tst_Users::supervisorAuthorizes()
{
    OPEN_USERS();
    QVERIFY(addUser(QStringLiteral("cajero"), QStringLiteral("Cajero")));
    QVERIFY(addUser(QStringLiteral("encargado"), QStringLiteral("Encargado")));

    int asked = 0;
    QString description, reason;
    m_users->setSupervisorPrompt([&](const QString &d, const QString &r, const QString &, QWidget *,
                                     QString *user, QString *password) {
        ++asked;
        description = d;
        reason = r;
        *user = QStringLiteral("encargado");
        *password = QStringLiteral("clave123");
        return true;
    });

    QVERIFY(signIn(QStringLiteral("cajero")));
    QVERIFY(m_users->authorize(QStringLiteral("inventario.baja"), QStringLiteral("Dar de baja: LECHE"), nullptr));
    QCOMPARE(asked, 1);
    QCOMPARE(description, QStringLiteral("Dar de baja"));
    QCOMPARE(reason, QStringLiteral("Dar de baja: LECHE"));
    QCOMPARE(m_users->currentUser()->username, QStringLiteral("cajero"));   // the session stays
    QVERIFY(!m_users->can(QStringLiteral("inventario.baja")));              // for this action only

    const AuditEntry entry = m_users->auditEntries().first();
    QCOMPARE(entry.action, QStringLiteral("autorizacion"));
    QCOMPARE(entry.user, QStringLiteral("cajero"));
    QCOMPARE(entry.authorizedBy, QStringLiteral("encargado"));
    QCOMPARE(entry.after, QStringLiteral("Dar de baja: LECHE"));

    // someone with the permission isn't asked
    QVERIFY(signIn(QStringLiteral("encargado")));
    QVERIFY(m_users->authorize(QStringLiteral("inventario.baja"), QStringLiteral("x"), nullptr));
    QCOMPARE(asked, 1);
}

void tst_Users::supervisorWithoutPermissionIsRefused()
{
    OPEN_USERS();
    QVERIFY(addUser(QStringLiteral("cajero"), QStringLiteral("Cajero")));
    QVERIFY(addUser(QStringLiteral("otro"), QStringLiteral("Cajero")));
    QVERIFY(addUser(QStringLiteral("encargado"), QStringLiteral("Encargado")));
    QVERIFY(signIn(QStringLiteral("cajero")));

    QStringList errors;
    m_users->setSupervisorPrompt([&](const QString &, const QString &, const QString &error, QWidget *,
                                     QString *user, QString *password) {
        errors << error;
        if (errors.size() == 1) {           // a colleague: no permission either
            *user = QStringLiteral("otro");
            *password = QStringLiteral("clave123");
            return true;
        }
        if (errors.size() == 2) {           // the right person, a wrong password
            *user = QStringLiteral("encargado");
            *password = QStringLiteral("mal");
            return true;
        }
        return false;                       // gives up
    });

    QVERIFY(!m_users->authorize(QStringLiteral("inventario.baja"), QStringLiteral("x"), nullptr));
    QCOMPARE(errors.size(), 3);
    QVERIFY(errors.at(0).isEmpty());
    QVERIFY2(errors.at(1).contains(QStringLiteral("no tiene permiso")), qPrintable(errors.at(1)));
    QVERIFY2(errors.at(2).contains(QStringLiteral("incorrectos")), qPrintable(errors.at(2)));
}

void tst_Users::cancelledAuthorization()
{
    OPEN_USERS();
    QVERIFY(addUser(QStringLiteral("cajero"), QStringLiteral("Cajero")));
    QVERIFY(signIn(QStringLiteral("cajero")));

    QVERIFY(!m_users->authorize(QStringLiteral("inventario.baja"), QStringLiteral("x"), nullptr));   // no prompt set

    m_users->setSupervisorPrompt([](const QString &, const QString &, const QString &, QWidget *, QString *, QString *) {
        return false;
    });
    QVERIFY(!m_users->authorize(QStringLiteral("inventario.baja"), QStringLiteral("x"), nullptr));
    QVERIFY(!m_users->lastError().isEmpty());
}

void tst_Users::auditRecordsSessions()
{
    OPEN_USERS();
    m_users->logout();
    QVERIFY(!m_users->login(QStringLiteral("admin"), QStringLiteral("mal")));
    QVERIFY(m_users->login(QStringLiteral("admin"), QStringLiteral("secreto1")));

    const QList<AuditEntry> entries = m_users->auditEntries();
    QVERIFY(entries.size() >= 4);
    QCOMPARE(entries.at(0).action, QStringLiteral("sesion.ingreso"));
    QCOMPARE(entries.at(1).action, QStringLiteral("sesion.fallida"));
    QCOMPARE(entries.at(1).user, QStringLiteral("admin"));
    QCOMPARE(entries.at(2).action, QStringLiteral("sesion.salida"));
    for (const AuditEntry &e : entries) {
        QVERIFY(!e.before.contains(QStringLiteral("secreto1")) && !e.after.contains(QStringLiteral("secreto1")));
        QVERIFY(!e.before.contains(QStringLiteral("mal")) && !e.after.contains(QStringLiteral("pbkdf2")));
    }
}

void tst_Users::auditRecordsProducts()
{
    OPEN_USERS();
    Product p;
    p.code = QStringLiteral("777");
    p.description = QStringLiteral("GASEOSA");
    p.price = 15000;
    QVERIFY(m_core.products->saveProduct(p));
    p.price = 16500;
    QVERIFY(m_core.products->saveProduct(p, p.code));
    QVERIFY(m_core.products->deactivateProduct(p.code));
    int imported = 0;
    Product q;
    q.code = QStringLiteral("888");
    q.description = QStringLiteral("AGUA");
    QVERIFY(m_core.products->importProducts({ q }, &imported));

    const QList<AuditEntry> entries = m_users->auditEntries();
    QCOMPARE(entries.at(0).action, QStringLiteral("producto.importar"));
    QCOMPARE(entries.at(1).action, QStringLiteral("producto.baja"));
    QCOMPARE(entries.at(2).action, QStringLiteral("producto.modificar"));
    QVERIFY2(entries.at(2).before.contains(QStringLiteral("precio 15.000")), qPrintable(entries.at(2).before));
    QVERIFY2(entries.at(2).after.contains(QStringLiteral("precio 16.500")), qPrintable(entries.at(2).after));
    QCOMPARE(entries.at(3).action, QStringLiteral("producto.crear"));
    QCOMPARE(entries.at(3).user, QStringLiteral("admin"));
    QCOMPARE(entries.at(3).entityId, QStringLiteral("777"));
    QVERIFY(!entries.at(3).station.isEmpty());
    QVERIFY(entries.at(3).when.isValid());
    QVERIFY(qAbs(entries.at(3).when.secsTo(QDateTime::currentDateTime())) < 120);
}

void tst_Users::auditFilter()
{
    OPEN_USERS();
    Product p;
    p.code = QStringLiteral("777");
    p.description = QStringLiteral("GASEOSA");
    QVERIFY(m_core.products->saveProduct(p));

    AuditFilter filter;
    filter.text = QStringLiteral("GASEOSA");
    QCOMPARE(m_users->auditEntries(filter).size(), 1);      // in the values, any case
    filter.text = QStringLiteral("nada de esto");
    QCOMPARE(m_users->auditEntries(filter).size(), 0);

    AuditFilter today;
    today.from = today.to = QDate::currentDate();
    QVERIFY(m_users->auditEntries(today).size() >= 2);
    AuditFilter past;
    past.to = QDate::currentDate().addDays(-2);
    QCOMPARE(m_users->auditEntries(past).size(), 0);

    AuditFilter one;
    one.limit = 1;
    QCOMPARE(m_users->auditEntries(one).size(), 1);
}

void tst_Users::inactivityWatcher()
{
    InactivityWatcher watcher;
    QSignalSpy idle(&watcher, &InactivityWatcher::idle);
    watcher.setInterval(150);
    QVERIFY(idle.wait(1000));

    // activity keeps it quiet
    idle.clear();
    watcher.setInterval(300);
    QObject target;
    for (int i = 0; i < 8; ++i) {
        QEvent input(QEvent::KeyPress);
        QCoreApplication::sendEvent(&target, &input);
        QTest::qWait(100);
    }
    QCOMPARE(idle.count(), 0);

    watcher.pause();
    QTest::qWait(450);
    QCOMPARE(idle.count(), 0);

    watcher.setInterval(0);                 // off
    QTest::qWait(100);
    QCOMPARE(idle.count(), 0);
}

void tst_Users::usernamesHaveNoSpaces()
{
    AGUARA_OPEN_FIXTURE(m_core, m_tmp.path());
    m_users.reset(new SqlUserService(m_core.monitor.get()));
    QVERIFY(!m_users->createFirstAdmin(QStringLiteral("el jefe"), QStringLiteral("Jefe"), QStringLiteral("secreto1")));
    QVERIFY(m_users->lastError().contains(QStringLiteral("espacios")));
    QVERIFY(m_users->createFirstAdmin(QStringLiteral("jefe"), QStringLiteral("Jefe"), QStringLiteral("secreto1")));

    UserAccount user;
    user.fullName = QStringLiteral("Ana Perez");
    user.username = QStringLiteral("ana perez");
    user.roleIds << m_users->roles().first().id;
    QVERIFY(!m_users->saveUser(user, QStringLiteral("clave123")));
    QVERIFY(m_users->lastError().contains(QStringLiteral("espacios")));
    user.username = QStringLiteral("ana.perez");
    QVERIFY(m_users->saveUser(user, QStringLiteral("clave123")));
}

void tst_Users::discountLimitComesFromTheRoles()
{
    OPEN_USERS();
    QCOMPARE(m_users->discountLimit(), 100);        // the administrator
    UserRole cashier;
    for (const UserRole &r : m_users->roles()) {
        if (r.name == QLatin1String("Cajero"))
            cashier = r;
        if (r.name == QLatin1String("Encargado"))
            QCOMPARE(r.maxDiscount, 20);
    }
    QCOMPARE(cashier.maxDiscount, 5);
    cashier.maxDiscount = 8;
    QVERIFY(m_users->saveRole(cashier));

    UserAccount user;
    user.username = QStringLiteral("nina");
    user.fullName = QStringLiteral("Nina");
    user.roleIds << cashier.id;
    QVERIFY(m_users->saveUser(user, QStringLiteral("clave123")));
    m_users->logout();
    QCOMPARE(m_users->discountLimit(), 0);          // nobody signed in
    QVERIFY(m_users->login(QStringLiteral("nina"), QStringLiteral("clave123")));
    QCOMPARE(m_users->discountLimit(), 8);
}

void tst_Users::lastAuthorizerNamesWhoAnswered()
{
    OPEN_USERS();
    QVERIFY(m_users->authorize(QStringLiteral("usuarios.administrar"), QStringLiteral("x"), nullptr));
    QCOMPARE(m_users->lastAuthorizer(), m_users->currentUser()->username);

    UserAccount user;
    for (const UserRole &r : m_users->roles()) {
        if (r.name == QLatin1String("Cajero"))
            user.roleIds << r.id;
    }
    user.username = QStringLiteral("nina");
    user.fullName = QStringLiteral("Nina");
    QVERIFY(m_users->saveUser(user, QStringLiteral("clave123")));
    const QString admin = m_users->currentUser()->username;
    m_users->logout();
    QVERIFY(m_users->login(QStringLiteral("nina"), QStringLiteral("clave123")));
    m_users->setSupervisorPrompt([&](const QString &, const QString &, const QString &, QWidget *,
                                     QString *username, QString *password) {
        *username = admin;
        *password = QStringLiteral("secreto1");
        return true;
    });
    QVERIFY(m_users->authorize(QStringLiteral("usuarios.administrar"), QStringLiteral("x"), nullptr));
    QCOMPARE(m_users->lastAuthorizer(), admin);
}

QTEST_GUILESS_MAIN(tst_Users)
#include "tst_users.moc"
