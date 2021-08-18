#ifndef PAGECERO_H
#define PAGECERO_H

#include <QWidget>

namespace Ui {
class PageCero;
}

class PageCero : public QWidget
{
    Q_OBJECT
public:
    explicit PageCero(QWidget *parent = nullptr);

    void addWidget(QWidget* widget);

private:
    Ui::PageCero *ui;
    int cols = 3;
    int rows = 4;
    int w;
    int h;
    int count = 0;
};

#endif // PAGECERO_H
