#ifndef MYMODULE_H
#define MYMODULE_H

#include <interfaces.h>


QT_BEGIN_NAMESPACE
namespace Ui { class MyModule; }
QT_END_NAMESPACE

class MyModule : public AguaraWidget
{

public:
    MyModule(QWidget *parent = nullptr);
    ~MyModule();

private:
    Ui::MyModule *ui;
};
#endif // MYMODULE_H
