#ifndef PAGECERO_H
#define PAGECERO_H

#include <QList>
#include <QPointer>
#include <QWidget>

namespace Ui {
class PageCero;
}

/// The permanent "Notifications" tab: notification cards in a grid.
class PageCero : public QWidget
{
    Q_OBJECT
public:
    explicit PageCero(QWidget *parent = nullptr);
    ~PageCero();

    /// Add a card. When it is deleted (dismissed/expired) the grid closes the gap.
    void addWidget(QWidget* widget);

private:
    void relayout();

    Ui::PageCero *ui;
    int cols = 3;
    int w;
    int h;
    QList<QPointer<QWidget>> items;
};

#endif // PAGECERO_H
