#ifndef MODIFYTABLEDIALOG_H
#define MODIFYTABLEDIALOG_H

#include <QDialog>

QT_BEGIN_NAMESPACE
namespace Ui {
class modifyTableDialog;
}
class QSqlQueryModel;
class QCompleter;
QT_END_NAMESPACE

class modifyTableDialog : public QDialog
{
    Q_OBJECT

public:
    explicit modifyTableDialog(QSqlQueryModel *model, bool modifyBrand = true, QWidget *parent = nullptr);
    ~modifyTableDialog();

protected:
    //void mouseReleaseEvent(QMouseEvent *event) override;

private:
    Ui::modifyTableDialog *ui;
    QCompleter *searchCompleter;
    QSqlQueryModel *model;
    QString table;

    void deleteRow(QString val);
    bool eventFilter(QObject *, QEvent*) override;

private slots:
    void onSearchTextChanged(const QString &text);
    void onCompleterAccepted(const QModelIndex &index);
    void onAddItem();
    void onRemoveItem();
    void doQueryAndRefresh(QString sql);
};

#endif // MODIFYTABLEDIALOG_H
