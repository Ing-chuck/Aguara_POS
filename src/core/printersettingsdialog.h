#ifndef PRINTERSETTINGSDIALOG_H
#define PRINTERSETTINGSDIALOG_H

#include <printservice.h>

#include <QDialog>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
QT_END_NAMESPACE

/// Preferencias > Impresora y comercio: this computer's printer, and the
/// store's data, which every register shares.
class PrinterSettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PrinterSettingsDialog(PrintService *print, QWidget *parent = nullptr);

    void accept() override;

private:
    QWidget *buildPrinterPage(const PrinterSettings &settings);
    QWidget *buildStorePage(const StoreInfo &store);
    PrinterSettings settings() const;
    void updateOptions();
    void testPrint();
    void showStatus(const QString &text, const char *role);

    PrintService *print;
    QComboBox *kind;
    QComboBox *printerName;
    QComboBox *paper;
    QCheckBox *everySale;
    QCheckBox *openDrawer;
    QCheckBox *cut;
    QPushButton *testButton;
    QLineEdit *storeName;
    QLineEdit *ruc;
    QLineEdit *address;
    QLineEdit *phone;
    QPlainTextEdit *footer;
    QLabel *status;
};

#endif // PRINTERSETTINGSDIALOG_H
