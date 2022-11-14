#ifndef SALESMODULE_H
#define SALESMODULE_H


#include <interfaces.h>

QT_BEGIN_NAMESPACE
namespace Ui { class SalesModule; }
QT_END_NAMESPACE

class SalesModule : public AguaraWidget
{

public:
    SalesModule(QWidget *parent = nullptr);
    ~SalesModule();

private:
    Ui::SalesModule *ui;
};
#endif // SALESMODULE_H
