#ifndef INTERFACES_H
#define INTERFACES_H

#include "messages.h"

#include <QWidget>
#include <QtPlugin>
#include <QStringList>
#include <QIcon>
#include <QSharedPointer>

QT_BEGIN_NAMESPACE
//forward declared QT dependencies go here
QT_END_NAMESPACE

/// Base class to all Aguara Modules (plugins)
class AguaraModule {
public:
    virtual ~AguaraModule() = default;

    bool registerModule(QString version) const;
    QString getName() const;
    QIcon getIcon() const;
    bool isMultiInstance() const;

protected:
    virtual QString compiledVersion() const = 0;

    QString name;
    QIcon icon;
    bool multiInstance;
};

/// Base class for factory interfaces
class AmFactory: public QWidget, public AguaraModule {
    Q_OBJECT
public:
    explicit AmFactory(QWidget* parent = nullptr);

signals:
    void notify(ModuleMsg msg);

protected:
    virtual QSharedPointer<QWidget> makeInstance() = 0;
};

/// Fabricator interface for single instance modules
class AmSingleInstanceFactory: public AmFactory {
public:
    AmSingleInstanceFactory(QWidget* parent = nullptr);
    QSharedPointer<QWidget> getInstance();

protected:
    QWeakPointer<QWidget> instance;
};

/// Fabricator interface for multi instance modules
class AmMultiInstanceFactory: public AmFactory {
public:
    AmMultiInstanceFactory(QWidget* parent = nullptr);
    //int getInstanceCount() const;
    //QList<QSharedPointer<QObject>> getInstances() const;
    //QSharedPointer<QObject> getInstance(int idx) const;
    QSharedPointer<QWidget> getInstance();

protected:
    //QList<QSharedPointer<QObject>> instances;
};

class ImportArticlesInterface: public AguaraModule {
public:
    virtual ~ImportArticlesInterface() = default;

    virtual QStringList FileTypes() const = 0;
    virtual bool ImportFrom(QString filename) = 0;
};

QT_BEGIN_NAMESPACE

#define aguaraModuleInterface_iid        "org.AguaraPOS.aguaraModuleInterface/1.0"
#define amSingleInstanceInterface_iid    "org.AguaraPOS.amSingleInstanceInterface/1.0"
#define amMultiInstanceInterface_iid     "org.AguaraPOS.amMultiInstanceInterface/1.0"
#define importArticlesInterface_iid      "org.AguaraPOS.importArticlesInterface/1.0"

Q_DECLARE_INTERFACE(ImportArticlesInterface, importArticlesInterface_iid)
Q_DECLARE_INTERFACE(AguaraModule, aguaraModuleInterface_iid)
Q_DECLARE_INTERFACE(AmSingleInstanceFactory, amSingleInstanceInterface_iid)
Q_DECLARE_INTERFACE(AmMultiInstanceFactory, amMultiInstanceInterface_iid)

QT_END_NAMESPACE

#endif // INTERFACES_H
