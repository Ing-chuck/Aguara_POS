#ifndef REPORTSPAGE_H
#define REPORTSPAGE_H

#include <interfaces.h>

class QComboBox;
class QDateEdit;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QWidget;

/// The list of reports, what each one asks for, and the result.
class ReportsPage : public AguaraWidget
{
    Q_OBJECT
public:
    ReportsPage(ReportService *reports, UserService *users, SalesService *sales, QWidget *parent = nullptr);

protected:
    void showEvent(QShowEvent *event) override;

private:
    void fillList();
    void fillChoices();
    void select();
    void execute();
    void showResult();
    void showProblem(const QString &text);
    void setPeriod(const QDate &from, const QDate &to);
    void exportCsv();
    void exportPdf();
    void printReport();
    const ReportDefinition *current() const;

    ReportService *reports;
    UserService *users;
    SalesService *sales;
    QList<ReportDefinition> definitions;
    std::optional<ReportResult> result;

    QListWidget *list;
    QLabel *title;
    QWidget *datesRow;
    QDateEdit *from;
    QDateEdit *to;
    QComboBox *cashRegister;
    QComboBox *user;
    QComboBox *category;
    QSpinBox *days;
    QLabel *daysLabel;
    QLineEdit *text;
    QTableWidget *table;
    QLabel *subtitle;
    QLabel *note;
    QLabel *problem;
    QPushButton *csvButton;
    QPushButton *pdfButton;
    QPushButton *printButton;
};

#endif // REPORTSPAGE_H
