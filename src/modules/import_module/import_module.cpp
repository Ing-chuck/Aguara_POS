#include "import_module.h"

QStringList import_module::features() const {
    QStringList featureList;
    featureList.append("Import Article database from CSV files");

    return featureList;
}

bool import_module::importFrom(QString filename)
{
    if(filename.isEmpty())
        return false;
    return true;
}
