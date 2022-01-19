#include "modifytabledialog.h"
#include "ui_modifytabledialog.h"

#include <QInputEvent>
#include <QtSql>
#include <QCompleter>
#include <QPushButton>
#include <QMessageBox>
#include <QRegularExpression>

modifyTableDialog::modifyTableDialog(QSqlQueryModel *model, bool modifyBrand, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::modifyTableDialog),
    model(model)
{
    // Window configuration
    setWindowFlag(Qt::FramelessWindowHint);
    setAttribute(Qt::WA_DeleteOnClose);
    //QCoreApplication::instance()->installEventFilter(this);
    parent->setEnabled(false);
    setEnabled(true);

    ui->setupUi(this);

    // Make line edits write in all caps
    //QObject::connect(ui->lineEdit_search, &QLineEdit::textEdited, this, &modifyTableDialog::onSearchTextChanged);
    QObject::connect(ui->lineEdit_search, &QLineEdit::textEdited, this, [&](const QString &text){ui->lineEdit_search->setText(text.toUpper());});
    QObject::connect(ui->lineEdit_add, &QLineEdit::textEdited, this, [&](const QString &text){ui->lineEdit_add->setText(text.toUpper());});

    if(modifyBrand) {
        ui->label_title->setText(tr("Actualizar Tabla de Marcas"));
        table = "brands";
        model->setHeaderData(0, Qt::Horizontal, tr("Marcas"));
    }
    else {
        ui->label_title->setText(tr("Actualizar Tabla de Rubros"));
        table = "busines_types";
        model->setHeaderData(0, Qt::Horizontal, tr("Rubros"));
    }

    ui->tableView->setModel(model);
    ui->tableView->verticalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    // Search completer
    searchCompleter = new QCompleter(model, this);
    searchCompleter->setCaseSensitivity(Qt::CaseInsensitive);
    searchCompleter->setCompletionMode(QCompleter::InlineCompletion);
    ui->lineEdit_search->setCompleter(searchCompleter);

    connect(searchCompleter, QOverload<const QModelIndex &>::of(&QCompleter::highlighted), this, &modifyTableDialog::onCompleterAccepted);

    // Connect add and remove buttons
    QObject::connect(ui->button_add, &QPushButton::clicked, this, &modifyTableDialog::onAddItem);
    QObject::connect(ui->button_remove, &QPushButton::clicked, this, &modifyTableDialog::onRemoveItem);
}

modifyTableDialog::~modifyTableDialog()
{
    //QCoreApplication::instance()->removeEventFilter(this);
    this->parentWidget()->setEnabled(true);
    delete ui;
}

void modifyTableDialog::onSearchTextChanged(const QString &text)
{
    ui->lineEdit_search->setText(text.toUpper());
}

void modifyTableDialog::onCompleterAccepted(const QModelIndex &index)
{
    qDebug() << __FUNCTION__;
    ui->tableView->selectionModel()->select(index,QItemSelectionModel::Select);
}

void modifyTableDialog::onAddItem()
{
    if(ui->lineEdit_add->text() != "")
    {
        QString name = ui->lineEdit_add->text();
        QString col = (table == "brands")? "brand_name" : "type";
        QString sql = QString("INSERT INTO %1 ('%2') VALUES('%3')").arg(table, col, name);
        doQueryAndRefresh(sql);
        ui->lineEdit_add->setText("");
    }
}

void modifyTableDialog::onRemoveItem()
{
    if(ui->tableView->selectionModel()->hasSelection())
    {
        QModelIndex indx = ui->tableView->currentIndex();
        QString warningTitle = tr("ATENCION");
        QString warningBody = tr("Esta accion es permanente\n esta seguro que desea continuar?");
        QMessageBox::StandardButton btn = QMessageBox::warning(this, warningTitle, warningBody, QMessageBox::Ok|QMessageBox::Cancel, QMessageBox::Cancel);
        if(btn == QMessageBox::Ok)
        {
            deleteRow(model->data(indx).toString());
        }
    }
}

void modifyTableDialog::deleteRow(QString val)
{
    qDebug() << "atempting to delete: " << val;

    QString col = (table == "brands")? "brand_name" : "type";
    QString sql = "DELETE FROM " + table + " WHERE " + col + " = '" + val + "'";

    doQueryAndRefresh(sql);
}

void modifyTableDialog::doQueryAndRefresh(QString sql)
{
    QSqlQuery query = model->query();
    QString lastQuery = query.executedQuery();
    query.exec(sql);
    if (model->lastError().isValid())
        qDebug() << model->lastError();

    // Refresh model
    model->setQuery(lastQuery);
}

bool modifyTableDialog::eventFilter(QObject *obj, QEvent *e)
{
    if (!dynamic_cast<QInputEvent*>(e))
        return false;

    QObject* target = this->parent();
    while(obj != NULL)
    {
        if(obj == target)
            return true;
        obj = obj->parent();
    }
    return false;
}
