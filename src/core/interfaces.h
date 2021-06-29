#ifndef INTERFACES_H
#define INTERFACES_H

#include <QtPlugin>
#include <QStringList>

QT_BEGIN_NAMESPACE
//QT dependencies go here
QT_END_NAMESPACE

class importArticlesInterface
{
public:
    virtual ~importArticlesInterface() = default;

    virtual QStringList features() const = 0;
    virtual bool importFrom(QString filename) = 0;
};

QT_BEGIN_NAMESPACE

#define importArticlesInterface_iid "org.AguaraPOS.importArticlesInterface/1.0"

Q_DECLARE_INTERFACE(importArticlesInterface, importArticlesInterface_iid)

QT_END_NAMESPACE

#endif // INTERFACES_H
