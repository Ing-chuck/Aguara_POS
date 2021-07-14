#ifndef INTERFACES_H
#define INTERFACES_H

#include <QtPlugin>
#include <QStringList>

QT_BEGIN_NAMESPACE
//QT dependencies go here
QT_END_NAMESPACE

class AguaraModule {
public:
    virtual ~AguaraModule() = default;

    virtual bool registerModule(QString version) const = 0;
};

class ImportArticlesInterface: public AguaraModule
{
public:
    virtual ~ImportArticlesInterface() = default;

    virtual QStringList FileTypes() const = 0;
    virtual bool ImportFrom(QString filename) = 0;
};

QT_BEGIN_NAMESPACE

#define importArticlesInterface_iid "org.AguaraPOS.importArticlesInterface/1.0"

Q_DECLARE_INTERFACE(ImportArticlesInterface, importArticlesInterface_iid)

QT_END_NAMESPACE

#endif // INTERFACES_H
