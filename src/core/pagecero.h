#ifndef PAGECERO_H
#define PAGECERO_H

#include <QList>
#include <QPointer>
#include <QWidget>

namespace Ui {
class PageCero;
}

/// The "Avisos" (notifications) page: a column of cards, newest first, or an
/// empty state when there are none. It lives as long as the window: closing
/// its tab only hides it, and cards keep arriving.
class PageCero : public QWidget
{
    Q_OBJECT
public:
    explicit PageCero(QWidget *parent = nullptr);
    ~PageCero();

    /// Add a card at the top. When it is deleted (dismissed, expired) the
    /// list closes the gap.
    void addWidget(QWidget* widget);

    int count() const;

signals:
    void countChanged(int count);

public slots:
    void dismissAll();

private:
    void refresh();

    Ui::PageCero *ui;
    QList<QPointer<QWidget>> items;
};

#endif // PAGECERO_H
