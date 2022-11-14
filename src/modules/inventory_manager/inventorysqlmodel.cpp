#include <QtSql>
#include <QDate>

#include "inventorysqlmodel.h"

// Helper Enum
// Defines column positions of each field
enum FieldPosition
{
    Code = 0,
    Description,
    Price,
    Cost,
    Stock,
    StockMin,
    DateAdded,
    DateModif,
    Active,
    Brand,
    Type,
    Vat,
    Provider
};

InventorySqlModel::InventorySqlModel(QObject *parent)
    : QSqlRelationalTableModel(parent)
{
    QObject::connect(this, &InventorySqlModel::beforeUpdate, this, &InventorySqlModel::onItemChanged);
}

Qt::ItemFlags InventorySqlModel::flags(const QModelIndex &index) const
{
    Qt::ItemFlags flags = QSqlTableModel::flags(index);
    if (index.column() > 8)
        flags = Qt::ItemIsEnabled|Qt::ItemIsSelectable; //&= !Qt::ItemIsEditable;
    return flags;
}

void InventorySqlModel::setTable(const QString &table)
{
    QSqlRelationalTableModel::setTable(table);
    renameHeaders();
    this->useForeignKeys();
}

void InventorySqlModel::renameHeaders()
{
    // Set the localized header captions:
    setHeaderData(FieldPosition::Code       , Qt::Horizontal, tr("Codigo"));
    setHeaderData(FieldPosition::Description, Qt::Horizontal, tr("Descripcion"));
    setHeaderData(FieldPosition::Brand      , Qt::Horizontal, tr("Marca"));
    setHeaderData(FieldPosition::Type       , Qt::Horizontal, tr("Rubro"));
    setHeaderData(FieldPosition::Price      , Qt::Horizontal, tr("PrecioVenta"));
    setHeaderData(FieldPosition::Cost       , Qt::Horizontal, tr("Costo"));
    setHeaderData(FieldPosition::Stock      , Qt::Horizontal, tr("Stock"));
    setHeaderData(FieldPosition::StockMin   , Qt::Horizontal, tr("StockMin"));
    setHeaderData(FieldPosition::Vat        , Qt::Horizontal, tr("IVA"));
    setHeaderData(FieldPosition::DateAdded  , Qt::Horizontal, tr("Fecha de Compra"));
    setHeaderData(FieldPosition::DateModif  , Qt::Horizontal, tr("Fecha de Modificación"));
    setHeaderData(FieldPosition::Provider   , Qt::Horizontal, tr("Proveedor"));
}

void InventorySqlModel::onItemChanged(int /*row*/, QSqlRecord &record)
{
    record.setValue(FieldPosition::DateModif, QDate::currentDate().toString(Qt::ISODate));
    record.setGenerated(FieldPosition::DateModif, true);
}

void InventorySqlModel::useForeignKeys() {
    this->setJoinMode(QSqlRelationalTableModel::LeftJoin);  // Show rows with foreign keys set to null
    this->setRelation(this->fieldIndex("marca_id"), QSqlRelation("marcas", "id", "nombre"));
    this->setRelation(this->fieldIndex("rubro_id"), QSqlRelation("rubros", "id", "rubro"));
}

bool InventorySqlModel::select()
{
    //qDebug(this->selectStatement().toStdString().c_str());
    return QSqlRelationalTableModel::select();
}
