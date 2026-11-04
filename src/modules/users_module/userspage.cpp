#include "userspage.h"

#include <QCheckBox>
#include <QDateEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

using AguaraStyle::Color;

namespace {

QTableWidget *makeTable(const QStringList &headers)
{
    auto table = new QTableWidget(0, headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setAlternatingRowColors(true);
    table->setShowGrid(false);
    table->setWordWrap(false);
    table->verticalHeader()->hide();
    table->verticalHeader()->setDefaultSectionSize(34);
    table->horizontalHeader()->setHighlightSections(false);
    table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    return table;
}

QWidget *tabPage(QVBoxLayout **layout)
{
    auto page = new QWidget;
    *layout = new QVBoxLayout(page);
    (*layout)->setContentsMargins(24, 16, 24, 20);
    (*layout)->setSpacing(12);
    return page;
}

/// The form of a new or existing user.
class UserDialog : public QDialog
{
public:
    UserDialog(UserService *users, const UserAccount &user, QWidget *parent)
        : QDialog(parent), users(users), user(user)
    {
        const bool isNew = user.id == 0;
        setWindowTitle(isNew ? QObject::tr("Nuevo usuario") : QObject::tr("Modificar usuario"));
        setMinimumWidth(440);
        auto layout = new QVBoxLayout(this);
        layout->setContentsMargins(24, 20, 24, 18);
        layout->setSpacing(12);

        auto title = new QLabel(windowTitle());
        AguaraStyle::setRole(title, AguaraStyle::Title);
        layout->addWidget(title);

        auto form = new QFormLayout;
        form->setHorizontalSpacing(12);
        form->setVerticalSpacing(10);
        form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
        fullName = new QLineEdit(user.fullName);
        username = new QLineEdit(user.username);
        password = new QLineEdit;
        password->setEchoMode(QLineEdit::Password);
        password->setPlaceholderText(isNew ? QObject::tr("Al menos 6 caracteres")
                                           : QObject::tr("Dejar vacío para no cambiarla"));
        active = new QCheckBox(QObject::tr("Puede ingresar al programa"));
        active->setChecked(user.active);
        form->addRow(QObject::tr("Nombre:"), fullName);
        form->addRow(QObject::tr("Usuario:"), username);
        form->addRow(QObject::tr("Contraseña:"), password);
        form->addRow(QObject::tr("Activo:"), active);
        layout->addLayout(form);

        auto section = new QLabel(QObject::tr("ROLES"));
        AguaraStyle::setRole(section, AguaraStyle::Section);
        layout->addWidget(section);
        for (const UserRole &role : users->roles()) {
            auto box = new QCheckBox(role.description.isEmpty() ? role.name
                                     : QStringLiteral("%1  -  %2").arg(role.name, role.description));
            box->setChecked(user.roleIds.contains(role.id));
            box->setProperty("roleId", role.id);
            roleBoxes << box;
            layout->addWidget(box);
        }

        auto note = new QLabel(QObject::tr("Con una contraseña nueva, el usuario debe elegir la suya al ingresar."));
        note->setWordWrap(true);
        AguaraStyle::setRole(note, AguaraStyle::Muted);
        layout->addWidget(note);

        auto buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
        buttons->button(QDialogButtonBox::Save)->setText(QObject::tr("Guardar"));
        buttons->button(QDialogButtonBox::Save)->setDefault(true);
        buttons->button(QDialogButtonBox::Cancel)->setText(QObject::tr("Cancelar"));
        QObject::connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        QObject::connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addWidget(buttons);
    }

    void accept() override
    {
        UserAccount saved = user;
        saved.fullName = fullName->text();
        saved.username = username->text();
        saved.active = active->isChecked();
        saved.roleIds.clear();
        for (QCheckBox *box : qAsConst(roleBoxes)) {
            if (box->isChecked())
                saved.roleIds << box->property("roleId").toLongLong();
        }
        if (!users->saveUser(saved, password->text())) {
            QMessageBox::warning(this, QObject::tr("No se pudo guardar"), users->lastError());
            return;
        }
        QDialog::accept();
    }

private:
    UserService *users;
    UserAccount user;
    QLineEdit *fullName;
    QLineEdit *username;
    QLineEdit *password;
    QCheckBox *active;
    QList<QCheckBox *> roleBoxes;
};

} // namespace

UsersPage::UsersPage(UserService *users, QWidget *parent)
    : AguaraWidget(parent), users(users)
{
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto header = new QFrame;
    AguaraStyle::setRole(header, AguaraStyle::PageHeader);
    auto headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(24, 16, 24, 8);
    headerLayout->setSpacing(2);
    auto title = new QLabel(tr("Usuarios"));
    AguaraStyle::setRole(title, AguaraStyle::Title);
    auto subtitle = new QLabel(tr("Quién usa el programa, qué puede hacer y qué se hizo"));
    AguaraStyle::setRole(subtitle, AguaraStyle::Subtitle);
    headerLayout->addWidget(title);
    headerLayout->addWidget(subtitle);
    layout->addWidget(header);

    auto tabs = new QTabWidget;
    tabs->setObjectName(QStringLiteral("usersTabs"));
    tabs->setDocumentMode(true);
    if (users->can(QStringLiteral("usuarios.administrar"))) {
        tabs->addTab(buildUsersTab(), AguaraStyle::icon(QStringLiteral("user"), AguaraStyle::color(Color::TextMuted)), tr("Usuarios"));
        tabs->addTab(buildRolesTab(), AguaraStyle::icon(QStringLiteral("shield"), AguaraStyle::color(Color::TextMuted)), tr("Roles y permisos"));
        reloadUsers();
        reloadRoles();
    }
    if (users->can(QStringLiteral("auditoria.ver"))) {
        tabs->addTab(buildAuditTab(), AguaraStyle::icon(QStringLiteral("list"), AguaraStyle::color(Color::TextMuted)), tr("Auditoría"));
        reloadAudit();
    }
    connect(tabs, &QTabWidget::currentChanged, this, [this, tabs]() {
        if (auditTable && tabs->currentWidget() == auditTable->parentWidget())
            reloadAudit();          // what was done since the page opened
    });
    layout->addWidget(tabs, 1);
}

// --- Usuarios ----------------------------------------------------------------

QWidget *UsersPage::buildUsersTab()
{
    QVBoxLayout *layout;
    QWidget *page = tabPage(&layout);

    auto bar = new QHBoxLayout;
    bar->addStretch();
    editUserButton = new QPushButton(AguaraStyle::icon(QStringLiteral("edit")), tr("Editar"));
    editUserButton->setEnabled(false);
    auto add = new QPushButton(AguaraStyle::icon(QStringLiteral("plus"), AguaraStyle::color(Color::OnAccent)), tr("Nuevo usuario"));
    AguaraStyle::setRole(add, AguaraStyle::Primary);
    bar->addWidget(editUserButton);
    bar->addWidget(add);
    layout->addLayout(bar);

    usersTable = makeTable({ tr("Nombre"), tr("Usuario"), tr("Roles"), tr("Estado") });
    usersTable->setObjectName(QStringLiteral("usersTable"));
    usersTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    usersTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    layout->addWidget(usersTable, 1);

    connect(add, &QPushButton::clicked, this, &UsersPage::onNewUser);
    connect(editUserButton, &QPushButton::clicked, this, &UsersPage::onEditUser);
    connect(usersTable, &QTableWidget::activated, this, &UsersPage::onEditUser);
    connect(usersTable, &QTableWidget::itemSelectionChanged, this, [this]() {
        editUserButton->setEnabled(usersTable->currentRow() >= 0);
    });
    return page;
}

void UsersPage::reloadUsers()
{
    userList = users->users();
    usersTable->setRowCount(userList.size());
    for (int row = 0; row < userList.size(); ++row) {
        const UserAccount &u = userList.at(row);
        usersTable->setItem(row, 0, new QTableWidgetItem(u.fullName));
        usersTable->setItem(row, 1, new QTableWidgetItem(u.username));
        usersTable->setItem(row, 2, new QTableWidgetItem(u.roleNames.join(QStringLiteral(", "))));
        auto state = new QTableWidgetItem(u.active ? tr("Activo") : tr("Inactivo"));
        if (!u.active)
            state->setForeground(AguaraStyle::color(Color::TextMuted));
        usersTable->setItem(row, 3, state);
    }
}

void UsersPage::editUser(const UserAccount &user)
{
    UserDialog dialog(users, user, this);
    if (dialog.exec() == QDialog::Accepted)
        reloadUsers();
}

void UsersPage::onNewUser()
{
    editUser(UserAccount());
}

void UsersPage::onEditUser()
{
    const int row = usersTable->currentRow();
    if (row >= 0 && row < userList.size())
        editUser(userList.at(row));
}

// --- Roles -------------------------------------------------------------------

QWidget *UsersPage::buildRolesTab()
{
    QVBoxLayout *layout;
    QWidget *page = tabPage(&layout);
    auto columns = new QHBoxLayout;
    columns->setSpacing(16);

    auto left = new QVBoxLayout;
    left->setSpacing(8);
    rolesList = new QListWidget;
    rolesList->setObjectName(QStringLiteral("rolesList"));
    rolesList->setMaximumWidth(240);
    auto add = new QPushButton(AguaraStyle::icon(QStringLiteral("plus")), tr("Nuevo rol"));
    removeRoleButton = new QPushButton(AguaraStyle::icon(QStringLiteral("trash"), AguaraStyle::color(Color::Danger)), tr("Quitar rol"));
    AguaraStyle::setRole(removeRoleButton, AguaraStyle::Danger);
    left->addWidget(rolesList, 1);
    left->addWidget(add);
    left->addWidget(removeRoleButton);
    columns->addLayout(left);

    auto right = new QVBoxLayout;
    right->setSpacing(8);
    auto form = new QFormLayout;
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(8);
    roleName = new QLineEdit;
    roleDescription = new QLineEdit;
    form->addRow(tr("Nombre:"), roleName);
    form->addRow(tr("Descripción:"), roleDescription);
    right->addLayout(form);
    rolePermissions = new QTreeWidget;
    rolePermissions->setObjectName(QStringLiteral("rolePermissions"));
    rolePermissions->setHeaderHidden(true);
    rolePermissions->setSelectionMode(QAbstractItemView::NoSelection);
    right->addWidget(rolePermissions, 1);
    auto saveRow = new QHBoxLayout;
    saveRow->addStretch();
    saveRoleButton = new QPushButton(tr("Guardar rol"));
    AguaraStyle::setRole(saveRoleButton, AguaraStyle::Primary);
    saveRow->addWidget(saveRoleButton);
    right->addLayout(saveRow);
    columns->addLayout(right, 1);
    layout->addLayout(columns, 1);

    connect(rolesList, &QListWidget::currentRowChanged, this, &UsersPage::showRole);
    connect(add, &QPushButton::clicked, this, &UsersPage::onNewRole);
    connect(removeRoleButton, &QPushButton::clicked, this, &UsersPage::onRemoveRole);
    connect(saveRoleButton, &QPushButton::clicked, this, &UsersPage::onSaveRole);
    return page;
}

void UsersPage::reloadRoles()
{
    const int keep = rolesList->currentRow();
    roleList = users->roles();
    rolesList->clear();
    for (const UserRole &role : qAsConst(roleList))
        rolesList->addItem(role.name);
    rolesList->setCurrentRow(keep >= 0 && keep < roleList.size() ? keep : 0);
}

void UsersPage::showRole()
{
    const int row = rolesList->currentRow();
    const UserRole role = row >= 0 && row < roleList.size() ? roleList.at(row) : UserRole();
    const bool fixed = role.allPermissions;

    roleName->setText(role.name);
    roleName->setEnabled(!role.system);
    roleDescription->setText(role.description);
    roleDescription->setEnabled(!fixed);
    saveRoleButton->setEnabled(!fixed);
    removeRoleButton->setEnabled(role.id != 0 && !role.system);

    rolePermissions->clear();
    QHash<QString, QTreeWidgetItem *> groups;
    for (const PermissionInfo &p : users->permissions()) {
        QTreeWidgetItem *group = groups.value(p.group);
        if (!group) {
            group = new QTreeWidgetItem(rolePermissions, { p.group });
            group->setFlags(Qt::ItemIsEnabled);
            QFont bold = group->font(0);
            bold.setBold(true);
            group->setFont(0, bold);
            group->setExpanded(true);
            groups.insert(p.group, group);
        }
        auto item = new QTreeWidgetItem(group, { p.description });
        item->setData(0, Qt::UserRole, p.code);
        item->setFlags(fixed ? Qt::ItemIsUserCheckable : (Qt::ItemIsEnabled | Qt::ItemIsUserCheckable));
        item->setCheckState(0, fixed || role.permissions.contains(p.code) ? Qt::Checked : Qt::Unchecked);
    }
}

void UsersPage::onNewRole()
{
    UserRole role;
    role.name = tr("Rol nuevo");
    if (!users->saveRole(role)) {
        QMessageBox::warning(this, tr("No se pudo crear el rol"), users->lastError());
        return;
    }
    reloadRoles();
    rolesList->setCurrentRow(roleList.size() - 1);
    roleName->setFocus();
    roleName->selectAll();
}

void UsersPage::onSaveRole()
{
    const int row = rolesList->currentRow();
    if (row < 0 || row >= roleList.size())
        return;
    UserRole role = roleList.at(row);
    role.name = roleName->text();
    role.description = roleDescription->text();
    role.permissions.clear();
    for (int g = 0; g < rolePermissions->topLevelItemCount(); ++g) {
        QTreeWidgetItem *group = rolePermissions->topLevelItem(g);
        for (int i = 0; i < group->childCount(); ++i) {
            if (group->child(i)->checkState(0) == Qt::Checked)
                role.permissions << group->child(i)->data(0, Qt::UserRole).toString();
        }
    }
    if (!users->saveRole(role)) {
        QMessageBox::warning(this, tr("No se pudo guardar el rol"), users->lastError());
        return;
    }
    reloadRoles();
    if (usersTable)
        reloadUsers();
    emit notify(ModuleMsg(QString(), QStringLiteral("role-saved"), tr("Rol guardado"),
                          tr("Los cambios valen desde el próximo ingreso de cada usuario."),
                          MsgPriority::PriorityLow, 6));
}

void UsersPage::onRemoveRole()
{
    const int row = rolesList->currentRow();
    if (row < 0 || row >= roleList.size())
        return;
    if (!users->removeRole(roleList.at(row).id)) {
        QMessageBox::warning(this, tr("No se pudo quitar el rol"), users->lastError());
        return;
    }
    reloadRoles();
}

// --- Auditoría -----------------------------------------------------------------

QWidget *UsersPage::buildAuditTab()
{
    QVBoxLayout *layout;
    QWidget *page = tabPage(&layout);

    auto bar = new QHBoxLayout;
    bar->setSpacing(8);
    auditFrom = new QDateEdit(QDate::currentDate().addDays(-7));
    auditTo = new QDateEdit(QDate::currentDate());
    for (QDateEdit *edit : { auditFrom, auditTo }) {
        edit->setCalendarPopup(true);
        edit->setDisplayFormat(QStringLiteral("dd/MM/yyyy"));
    }
    auditText = new QLineEdit;
    auditText->setPlaceholderText(tr("Buscar usuario, acción o artículo"));
    AguaraStyle::searchField(auditText);
    bar->addWidget(new QLabel(tr("Desde")));
    bar->addWidget(auditFrom);
    bar->addWidget(new QLabel(tr("Hasta")));
    bar->addWidget(auditTo);
    bar->addSpacing(8);
    bar->addWidget(auditText, 1);
    layout->addLayout(bar);

    auditTable = makeTable({ tr("Fecha"), tr("Usuario"), tr("Acción"), tr("Sobre"), tr("Antes"), tr("Después"),
                             tr("Autorizó"), tr("Puesto") });
    auditTable->setObjectName(QStringLiteral("auditTable"));
    auditTable->setAlternatingRowColors(true);
    auditTable->horizontalHeader()->setStretchLastSection(false);
    auditTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    auditTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    layout->addWidget(auditTable, 1);

    connect(auditFrom, &QDateEdit::dateChanged, this, &UsersPage::reloadAudit);
    connect(auditTo, &QDateEdit::dateChanged, this, &UsersPage::reloadAudit);
    connect(auditText, &QLineEdit::textChanged, this, &UsersPage::reloadAudit);
    return page;
}

void UsersPage::reloadAudit()
{
    AuditFilter filter;
    filter.from = auditFrom->date();
    filter.to = auditTo->date();
    filter.text = auditText->text();
    const QList<AuditEntry> entries = users->auditEntries(filter);

    auditTable->setRowCount(entries.size());
    for (int row = 0; row < entries.size(); ++row) {
        const AuditEntry &e = entries.at(row);
        const QStringList cells = { e.when.toString(QStringLiteral("dd/MM/yyyy HH:mm:ss")), e.user, e.action, e.entityId,
                                    e.before, e.after, e.authorizedBy, e.station };
        for (int column = 0; column < cells.size(); ++column) {
            auto item = new QTableWidgetItem(cells.at(column));
            item->setToolTip(cells.at(column));
            auditTable->setItem(row, column, item);
        }
    }
    auditTable->resizeColumnToContents(0);
    auditTable->resizeColumnToContents(1);
    auditTable->resizeColumnToContents(2);
}
