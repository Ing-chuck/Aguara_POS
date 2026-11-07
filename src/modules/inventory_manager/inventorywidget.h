#ifndef INVENTORYWIDGET_H
#define INVENTORYWIDGET_H

#include <interfaces.h>

QT_BEGIN_NAMESPACE
namespace Ui { class inventoryWidget; }
class QAbstractItemModel;
class QTimer;
class SearchProxyModel;
class QMenu;
class QPushButton;
class QTabWidget;
QT_END_NAMESPACE

class addItemDialog;

class inventoryWidget : public AguaraWidget
{
    Q_OBJECT

public:
    /// `users` may be null (nothing is restricted then).
    explicit inventoryWidget(ProductService *products, InventoryService *inventory, UserService *users,
                             QWidget *parent = nullptr);
    ~inventoryWidget();

private:
    Ui::inventoryWidget *ui = nullptr;
    ProductService *products = nullptr;
    InventoryService *inventory = nullptr;
    UserService *users = nullptr;
    QTabWidget *sections = nullptr;
    QMenu *moreMenu = nullptr;
    QPushButton *moreButton = nullptr;
    addItemDialog *addDialog = nullptr;
    addItemDialog *editDialog = nullptr;
    QAbstractItemModel *model = nullptr;
    SearchProxyModel *visible = nullptr;    ///< the rows the search leaves on the table
    QTimer *searchTimer = nullptr;

    void loadTable();
    void setUpKeyboard();
    void fitColumns();
    bool eventFilter(QObject *watched, QEvent *event) override;
    QString selectedCode() const;
    bool authorized(const QString &permission, const QString &reason);
    void buildSections();

private slots:
    void focusSearch();
    void onAdjustStock();
    void onHistory();
    void onPriceChange();
    void onLabels();
    void onExpiryWarning();
    void onSearchEntered();
    void updateSummary();
    void updateActions();
    void onItemAddPressed();
    void onItemAddedEdited(QString id);
    void onItemEditPressed();
    void onItemRemovePressed();
};
#endif // INVENTORYWIDGET_H
