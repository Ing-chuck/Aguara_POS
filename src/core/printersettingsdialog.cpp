#include "printersettingsdialog.h"

#include <aguarastyle.h>
#include <customerservice.h>
#include <formcheck.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

PrinterSettingsDialog::PrinterSettingsDialog(PrintService *print, QWidget *parent)
    : QDialog(parent), print(print)
{
    setWindowTitle(tr("Impresora y comercio"));
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 18);
    layout->setSpacing(12);
    auto title = new QLabel(tr("Impresora y comercio"));
    AguaraStyle::setRole(title, AguaraStyle::Title);
    layout->addWidget(title);

    auto tabs = new QTabWidget;
    tabs->addTab(buildPrinterPage(print->printerSettings()), tr("Impresora de este equipo"));
    tabs->addTab(buildStorePage(print->storeInfo()), tr("Datos del comercio"));
    layout->addWidget(tabs, 1);

    status = new QLabel;
    status->setObjectName(QStringLiteral("printerStatus"));
    status->setWordWrap(true);
    status->hide();
    layout->addWidget(status);

    auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QPushButton *ok = buttons->button(QDialogButtonBox::Ok);
    ok->setText(tr("Guardar"));
    ok->setDefault(true);
    AguaraStyle::setRole(ok, AguaraStyle::Primary);
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancelar"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    updateOptions();
    resize(540, sizeHint().height());
}

QWidget *PrinterSettingsDialog::buildPrinterPage(const PrinterSettings &s)
{
    kind = new QComboBox;
    kind->setObjectName(QStringLiteral("printerKind"));
    kind->addItem(tr("Sin impresora"), int(PrinterKind::None));
    kind->addItem(tr("Térmica de tickets (ESC/POS)"), int(PrinterKind::Thermal));
    kind->addItem(tr("Impresora común (A4)"), int(PrinterKind::System));
    kind->setCurrentIndex(kind->findData(int(s.kind)));

    printerName = new QComboBox;
    printerName->setObjectName(QStringLiteral("printerName"));
    printerName->addItems(print->printers());
    if (!s.name.isEmpty() && printerName->findText(s.name) < 0)
        printerName->addItem(s.name);       // unplugged today: don't lose it
    printerName->setCurrentText(s.name);

    paper = new QComboBox;
    paper->setObjectName(QStringLiteral("printerPaper"));
    paper->addItem(tr("80 mm (48 columnas)"), 48);
    paper->addItem(tr("58 mm (32 columnas)"), 32);
    paper->setCurrentIndex(qMax(0, paper->findData(s.columns)));

    everySale = new QCheckBox(tr("Imprimir el ticket de cada venta"));
    everySale->setObjectName(QStringLiteral("printerEverySale"));
    everySale->setChecked(s.printEverySale);
    openDrawer = new QCheckBox(tr("Abrir el cajón al cobrar en efectivo"));
    openDrawer->setObjectName(QStringLiteral("printerOpenDrawer"));
    openDrawer->setChecked(s.openDrawerOnCash);
    cut = new QCheckBox(tr("Cortar el papel"));
    cut->setObjectName(QStringLiteral("printerCut"));
    cut->setChecked(s.cut);

    testButton = new QPushButton(tr("Imprimir prueba"));
    testButton->setObjectName(QStringLiteral("printerTest"));
    testButton->setAutoDefault(false);

    auto page = new QWidget;
    auto form = new QFormLayout(page);
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(10);
    form->addRow(tr("Tipo:"), kind);
    form->addRow(tr("Impresora:"), printerName);
    form->addRow(tr("Papel:"), paper);
    form->addRow(QString(), everySale);
    form->addRow(QString(), openDrawer);
    form->addRow(QString(), cut);
    form->addRow(QString(), testButton);

    connect(kind, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PrinterSettingsDialog::updateOptions);
    connect(testButton, &QPushButton::clicked, this, &PrinterSettingsDialog::testPrint);
    return page;
}

QWidget *PrinterSettingsDialog::buildStorePage(const StoreInfo &store)
{
    storeName = new QLineEdit(store.name);
    storeName->setObjectName(QStringLiteral("storeName"));
    ruc = new QLineEdit(store.ruc);
    ruc->setObjectName(QStringLiteral("storeRuc"));
    address = new QLineEdit(store.address);
    address->setObjectName(QStringLiteral("storeAddress"));
    phone = new QLineEdit(store.phone);
    phone->setObjectName(QStringLiteral("storePhone"));
    footer = new QPlainTextEdit(store.footer);
    footer->setObjectName(QStringLiteral("storeFooter"));
    footer->setPlaceholderText(tr("¡Gracias por su compra!"));
    footer->setTabChangesFocus(true);
    footer->setMaximumHeight(72);

    auto page = new QWidget;
    auto form = new QFormLayout(page);
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(10);
    form->addRow(AguaraForm::required(tr("Nombre:")), storeName);
    form->addRow(tr("RUC:"), ruc);
    form->addRow(tr("Dirección:"), address);
    form->addRow(tr("Teléfono:"), phone);
    form->addRow(tr("Pie del ticket:"), footer);
    form->addRow(QString(), AguaraForm::requiredNote());
    return page;
}

PrinterSettings PrinterSettingsDialog::settings() const
{
    PrinterSettings s;
    s.kind = PrinterKind(kind->currentData().toInt());
    s.name = printerName->currentText();
    s.columns = paper->currentData().toInt();
    s.printEverySale = everySale->isChecked();
    s.openDrawerOnCash = openDrawer->isChecked();
    s.cut = cut->isChecked();
    return s;
}

void PrinterSettingsDialog::updateOptions()
{
    const auto selected = PrinterKind(kind->currentData().toInt());
    const bool thermal = selected == PrinterKind::Thermal;
    const bool printing = selected != PrinterKind::None;
    printerName->setEnabled(printing);
    everySale->setEnabled(printing);
    testButton->setEnabled(printing);
    paper->setEnabled(thermal);
    openDrawer->setEnabled(thermal);        // the drawer hangs from the thermal printer
    cut->setEnabled(thermal);
}

void PrinterSettingsDialog::testPrint()
{
    if (print->printTest(settings()))
        showStatus(tr("Prueba enviada a %1.").arg(printerName->currentText()), AguaraStyle::Success);
    else
        showStatus(print->lastError(), AguaraStyle::Error);
}

void PrinterSettingsDialog::showStatus(const QString &text, const char *role)
{
    status->setText(text);
    AguaraStyle::setRole(status, role);
    status->show();
}

void PrinterSettingsDialog::accept()
{
    const PrinterSettings chosen = settings();
    if (chosen.kind != PrinterKind::None && chosen.name.isEmpty()) {
        showStatus(tr("Elija la impresora."), AguaraStyle::Error);
        return;
    }
    const auto refuse = [this](QLineEdit *field, const QString &text) {
        findChild<QTabWidget *>()->setCurrentWidget(field->parentWidget());
        showStatus(text, AguaraStyle::Error);
        field->setFocus();
        field->selectAll();
    };
    if (storeName->text().trimmed().isEmpty()) {
        refuse(storeName, tr("Escriba el nombre del comercio: encabeza los tickets."));
        return;
    }
    if (!ruc->text().trimmed().isEmpty() && !AguaraCustomer::validRuc(ruc->text().trimmed())) {
        refuse(ruc, tr("El RUC no es válido: revise el número y su dígito verificador (80009735-1)."));
        return;
    }
    if (!AguaraForm::validPhone(phone->text())) {
        refuse(phone, tr("El teléfono lleva solo números, por ejemplo 021 555 123."));
        return;
    }
    StoreInfo store;
    store.name = storeName->text();
    store.ruc = ruc->text();
    store.address = address->text();
    store.phone = phone->text();
    store.footer = footer->toPlainText();
    if (!print->saveStoreInfo(store)) {
        showStatus(print->lastError(), AguaraStyle::Error);
        return;
    }
    print->savePrinterSettings(chosen);
    QDialog::accept();
}
