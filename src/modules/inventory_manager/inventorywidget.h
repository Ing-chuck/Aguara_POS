#ifndef INVENTORYWIDGET_H
#define INVENTORYWIDGET_H

#include <interfaces.h>

QT_BEGIN_NAMESPACE
namespace Ui { class inventoryWidget; }
class QAbstractItemModel;
class QCompleter;
QT_END_NAMESPACE

class addItemDialog;

class inventoryWidget : public AguaraWidget
{
    Q_OBJECT

public:
    /// `users` may be null (nothing is restricted then).
    explicit inventoryWidget(ProductService *products, UserService *users, QWidget *parent = nullptr);
    ~inventoryWidget();

private:
    Ui::inventoryWidget *ui = nullptr;
    ProductService *products = nullptr;
    UserService *users = nullptr;
    addItemDialog *addDialog = nullptr;
    addItemDialog *editDialog = nullptr;
    QAbstractItemModel *model = nullptr;
    QCompleter *searchCompleter = nullptr;

    void loadTable();
    void setUpKeyboard();
    void fitColumns();
    bool eventFilter(QObject *watched, QEvent *event) override;
    QString selectedCode() const;
    bool authorized(const QString &permission, const QString &reason);

private slots:
    void focusSearch();
    void onSearchEntered();
    void updateSummary();
    void updateActions();
    void onItemAddPressed();
    void onItemAddedEdited(QString id);
    void onItemEditPressed();
    void onItemRemovePressed();
    void onCompleterAccepted(const QModelIndex &index);
};
#endif // INVENTORYWIDGET_H
