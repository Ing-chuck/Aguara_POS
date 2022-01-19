#ifndef CUSTOMSQLMODEL_H
#define CUSTOMSQLMODEL_H


#include <QSqlTableModel>

class CustomSqlModel : public QSqlTableModel
{
    Q_OBJECT

public:
    CustomSqlModel(QObject *parent = nullptr);

    Qt::ItemFlags flags(const QModelIndex &index) const override;

private:
    void refresh();

private slots:
    void onItemChanged(int row, QSqlRecord &record);
};

#endif // CUSTOMSQLMODEL_H
