#include <QtSql>
#include <QDate>

#include "customsqlmodel.h"

// Helper Enum
// Defines column positions of each field
enum FieldPosition
{
    Code = 0,
    Description,
    Brand,
    Type,
    Price,
    Cost,
    Stock,
    StockMin,
    Vat,
    DateAdded,
    DateModif
};

CustomSqlModel::CustomSqlModel(QObject *parent)
    : QSqlTableModel(parent)
{
    QObject::connect(this, &CustomSqlModel::beforeUpdate, this, &CustomSqlModel::onItemChanged);
}

Qt::ItemFlags CustomSqlModel::flags(const QModelIndex &index) const
{
    Qt::ItemFlags flags = QSqlTableModel::flags(index);
    if (index.column() > 8)
        flags = Qt::ItemIsEnabled|Qt::ItemIsSelectable; //&= !Qt::ItemIsEditable;
    return flags;
}

void CustomSqlModel::refresh()
{
    //setQuery("select * from person");
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
    setHeaderData(FieldPosition::DateAdded  , Qt::Horizontal, tr("F.Compra"));
    setHeaderData(FieldPosition::DateModif  , Qt::Horizontal, tr("F.Modif"));
}

void CustomSqlModel::onItemChanged(int /*row*/, QSqlRecord &record)
{
    record.setValue(FieldPosition::DateModif, QDate::currentDate().toString(Qt::ISODate));
    record.setGenerated(FieldPosition::DateModif, true);
}
