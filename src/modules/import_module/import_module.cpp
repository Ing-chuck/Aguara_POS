#include "import_module.h"
#include <version.h>

#include <QFile>
#include <QStringList>
#include <QSqlQuery>
#include <QSqlError>
#include <QTextStream>
#include <QMap>

ImportModule::ImportModule(QObject *parent)
    : ImportArticlesInterface(parent)
{
    this->name = "Importar";
    this->icon = QIcon(":/import_module/images/icon.png");
}

/// Register the module with the core program
/// compares the core program's version
/// with the module's version to determine compatibility
QString ImportModule::compiledVersion() const {
    return QString(VERSION_STRING);
}

/// Returns a list of features
/// describing the function of the module
QStringList ImportModule::FileTypes() const {
    QStringList typelist;
    typelist.append("csv");

    return typelist;
}

/// Read a csv file and update the SQLite database
bool ImportModule::ImportFrom(QString filename)
{
    QFile csv(filename);
    if(!csv.open(QIODevice::ReadOnly)) {
        return false;
    }
    QTextStream csvStream;
    csvStream.setDevice(&csv);

    qDebug() << filename;
    QMap<int, QString> headerMap;
    while(!csvStream.atEnd()){
        QStringList cols = csvStream.readLine().split(',');

        if(cols[1] == "")
            continue;
        // remove whaite spaces just in case
        for(QString &col : cols)
            col = col.trimmed();

        // checking if this is a header row
        // non header rows start with a number
        bool ok, headerRow = false;
        cols[0].toDouble(&ok);
        if(!ok) {
            //header row
            //qDebug() << cols[1];
            headerRow = true;
        }

        QList<QVariant> data;
        QStringList headers; // headers to which the data on this row corresponds to
        for(int i = 0; i < cols.size(); i++){
            if(cols[i] != ""){
                if(headerRow)
                    headerMap.insert(i, cols[i]);
                else {
                    // ensure data is on a valid column
                    // ie. there is a corresponding header
                    if(headerMap.contains(i) && cols[i] != "NULL"){
                        headers.append(headerMap[i]);
                        data.append(cols[i]);
                    }
                }
            }
        }

        if(!headerRow)
            insertInto(headers, data);
    }

    csv.close();
    return true;
}

void ImportModule::insertInto(QStringList fields, QList<QVariant> data) {
    QSqlQuery query;
    QStringList cols;
    QStringList vals;
    //;Código;;Descripción;;;Marca;;;Precio;Costo;Stock
    for(QString& field : fields){
        if(field == "Codigo") {
            cols.append("Codigo");
            vals.append(":cod");
        }
        if(field == "Descripcion") {
            cols.append("Descripcion");
            vals.append(":desc");
        }
        if(field == "Marca") {
            cols.append("Marca");
            vals.append(":bran");
        }
        if(field == "Rubro") {
            cols.append("Rubro");
            vals.append(":cat");
        }
        if(field == "PrecioVenta") {
            cols.append("PrecioVenta");
            vals.append(":sPrice");
        }
        if(field == "Costo") {
            cols.append("Costo");
            vals.append(":bPrice");
        }
        if(field == "Stock") {
            cols.append("Stock");
            vals.append(":stk");
        }
        if(field == "StockMin") {
            cols.append("StockMin");
            vals.append(":stkM");
        }
        if(field == "IVA") {
            cols.append("IVA");
            vals.append(":v");
        }
        if(field == "F_Compra") {
            cols.append("F_Compra");
            vals.append(":bDate");
        }
        if(field == "F_Modif") {
            cols.append("F_Modif");
            vals.append(":mDate");
        }
    }

    query.prepare("INSERT INTO articles (" + cols.join(", ") + ") VALUES (" + vals.join(", ") + ")");
    for(int i = 0; i < vals.size(); i++) {
        query.bindValue(vals[i], data[i]);
    }
    query.exec();
}
