#include "labelsheet.h"

#include <barcode.h>
#include <money.h>

#include <QPainter>
#include <QPrinter>

namespace {

void drawLabel(QPainter &painter, const QRectF &box, const Product &product)
{
    painter.save();
    QPen border(QColor(0xB8, 0xC0, 0xCA));
    border.setWidthF(0.5);
    painter.setPen(border);
    painter.drawRect(box);
    painter.setPen(Qt::black);

    const qreal margin = box.height() * 0.06;
    const QRectF inner = box.adjusted(margin, margin, -margin, -margin);

    QFont font(QStringLiteral("Segoe UI"));
    font.setPointSizeF(9);
    painter.setFont(font);
    const QRectF nameBox(inner.left(), inner.top(), inner.width(), inner.height() * 0.32);
    painter.drawText(nameBox, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, product.description);

    font.setPointSizeF(16);
    font.setBold(true);
    painter.setFont(font);
    const QRectF priceBox(inner.left(), nameBox.bottom(), inner.width(), inner.height() * 0.30);
    painter.drawText(priceBox, Qt::AlignLeft | Qt::AlignVCenter, AguaraMoney::formatWithSymbol(product.price));

    const QRectF codeBox(inner.left(), priceBox.bottom(), inner.width(), inner.bottom() - priceBox.bottom());
    const QString bars = AguaraBarcode::ean13Bars(product.code);
    font.setPointSizeF(7);
    font.setBold(false);
    painter.setFont(font);
    if (bars.isEmpty()) {
        painter.drawText(codeBox, Qt::AlignLeft | Qt::AlignBottom, product.code);
    } else {
        const QRectF digits = codeBox.adjusted(0, codeBox.height() * 0.72, 0, 0);
        painter.drawText(digits, Qt::AlignLeft | Qt::AlignVCenter, product.code);
        const qreal module = qMin(codeBox.width() / bars.size(), qreal(1.2 * painter.device()->logicalDpiX() / 25.4 / 3));
        for (int i = 0; i < bars.size(); ++i) {
            if (bars.at(i) == QLatin1Char('1'))
                painter.fillRect(QRectF(codeBox.left() + i * module, codeBox.top(), module,
                                        codeBox.height() * 0.70), Qt::black);
        }
    }
    painter.restore();
}

} // namespace

namespace LabelSheet {

int print(QPrinter *printer, const QList<Product> &products)
{
    QPainter painter(printer);
    if (!painter.isActive())
        return 0;
    const QRectF page = printer->pageRect(QPrinter::DevicePixel).translated(
        -printer->pageRect(QPrinter::DevicePixel).topLeft());
    const qreal width = page.width() / Columns;
    const qreal height = page.height() / Rows;

    int pages = products.isEmpty() ? 0 : 1;
    for (int i = 0; i < products.size(); ++i) {
        const int slot = i % (Columns * Rows);
        if (i > 0 && slot == 0) {
            printer->newPage();
            ++pages;
        }
        const QRectF box(page.left() + (slot % Columns) * width, page.top() + (slot / Columns) * height, width, height);
        drawLabel(painter, box, products.at(i));
    }
    return pages;
}

} // namespace LabelSheet
