#ifndef MYMODULE_H
#define MYMODULE_H


#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui { class MyModule; }
QT_END_NAMESPACE

class MyModule : public QWidget
{

public:
    MyModule(QWidget *parent = nullptr);
    ~MyModule();

private:
    Ui::MyModule *ui;
};
#endif // MYMODULE_H
