#ifndef INVENTORYSQLMODEL_H
#define INVENTORYSQLMODEL_H


#include <QSqlRelationalTableModel>

class InventorySqlModel : public QSqlRelationalTableModel
{
    Q_OBJECT

public:
    InventorySqlModel(QObject *parent = nullptr);

    Qt::ItemFlags flags(const QModelIndex &index) const override;
    void setTable(const QString &table) override;
    bool select() override;

    void useForeignKeys();

private:
    void renameHeaders();

private slots:
    void onItemChanged(int row, QSqlRecord &record);
};

#endif // INVENTORYSQLMODEL_H
