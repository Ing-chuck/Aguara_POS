#include "import_module.h"
#include "csvreader.h"
#include <version.h>

#include <QDate>
#include <QFile>
#include <QFileInfo>
#include <QHash>

namespace {

/// Product fields a CSV column can map to
enum class Field { Code, Description, Brand, Category, Price, Cost, Stock, StockMin, Vat, Purchased, Ignored };
inline uint qHash(Field f, uint seed = 0) { return ::qHash(int(f), seed); }

/// "Código" -> "codigo", "Precio Venta" -> "precioventa"
QString normalizeHeader(QString h)
{
    h = h.trimmed().toLower();
    const QString from = QStringLiteral("áéíóúüñ");
    const QString to   = QStringLiteral("aeiouun");
    for (int i = 0; i < from.size(); ++i)
        h.replace(from.at(i), to.at(i));
    h.remove(QLatin1Char(' '));
    h.remove(QLatin1Char('_'));
    return h;
}

Field fieldForHeader(const QString &header)
{
    static const QHash<QString, Field> fields = {
        { "codigo", Field::Code },           { "descripcion", Field::Description },
        { "marca", Field::Brand },           { "rubro", Field::Category },
        { "precioventa", Field::Price },     { "precio", Field::Price },
        { "costo", Field::Cost },            { "stock", Field::Stock },
        { "stockmin", Field::StockMin },     { "iva", Field::Vat },
        { "fcompra", Field::Purchased },     { "fechacompra", Field::Purchased },
    };
    return fields.value(normalizeHeader(header), Field::Ignored);
}

QDate parseDate(const QString &text)
{
    for (const char *format : { "yyyy-MM-dd", "dd/MM/yyyy", "yyyy/MM/dd", "d/M/yyyy" }) {
        const QDate d = QDate::fromString(text, QLatin1String(format));
        if (d.isValid())
            return d;
    }
    return QDate();
}

} // namespace

ImportModule::ImportModule(QObject *parent)
    : QObject(parent)
{
}

ModuleInfo ImportModule::moduleInfo() const {
    ModuleInfo info;
    info.id          = "import";
    info.name        = "Importar";
    info.description = "Importa artículos desde archivos CSV (por ejemplo, exportados de Excel)";
    info.version     = VERSION_STRING;
    info.iconName    = "import";    // theme icon (AguaraStyle::icon)
    return info;
}

bool ImportModule::initialize(AguaraHost *h)
{
    host = h;
    products = host->service<ProductService>();
    return products != nullptr;
}

/// File types this importer can read
QStringList ImportModule::fileTypes() const {
    return { "csv" };
}

void ImportModule::report(const QString &title, const QString &text, MsgPriority priority)
{
    host->notify(ModuleMsg(moduleInfo().name, "import", title, text, priority));
}

/// Read a CSV file of articles and save them through the ProductService.
/// The first row whose first cell isn't a number is taken as the header row;
/// columns are matched by name (Codigo, Descripcion, Marca, Rubro,
/// PrecioVenta/Precio, Costo, Stock, StockMin, IVA, F_Compra), others ignored.
/// The separator (',' or ';') is detected from the first line. Quoted fields
/// ("..." with "" for a quote) may contain the separator and line breaks.
/// Encoding: UTF-8, or Windows-1252 (Excel on Spanish Windows).
/// All rows are saved in one transaction: a database error saves nothing.
bool ImportModule::importFrom(const QString &filename)
{
    QFile csv(filename);
    if (!csv.open(QIODevice::ReadOnly)) {
        report(tr("Importación fallida"), tr("No se pudo abrir %1").arg(filename), MsgPriority::PriorityHigh);
        return false;
    }
    const QString text = Csv::decode(csv.readAll());
    csv.close();

    QHash<int, Field> columns;      // column index -> field, from the header row
    QList<Product> parsed;
    QStringList problems;

    for (const Csv::Record &record : Csv::parse(text, Csv::detectSeparator(text))) {
        const int lineNumber = record.line;
        QStringList cols = record.fields;
        // remove white spaces just in case
        for (QString &col : cols)
            col = col.trimmed();
        if (cols.join(QString()).isEmpty())
            continue;

        // checking if this is a header row
        // non header rows start with a number
        bool isNumber = false;
        cols[0].toDouble(&isNumber);
        if (!isNumber && columns.isEmpty()) {
            for (int i = 0; i < cols.size(); i++) {
                const Field f = fieldForHeader(cols[i]);
                if (f != Field::Ignored)
                    columns.insert(i, f);
            }
            continue;
        }
        if (columns.isEmpty())
            continue;               // data before any header: nothing to map it to

        // Collect this row's values by field
        QHash<Field, QString> values;
        for (auto it = columns.cbegin(); it != columns.cend(); ++it) {
            const QString value = it.key() < cols.size() ? cols[it.key()] : QString();
            if (!value.isEmpty() && value != "NULL")
                values.insert(it.value(), value);
        }

        // An existing product keeps whatever this file doesn't mention
        // (e.g. a price list without a Stock column doesn't reset stock).
        Product p;
        p.purchased = QDate::currentDate();
        if (const std::optional<Product> existing = products->product(values.value(Field::Code))) {
            p = *existing;
            p.brandId = p.categoryId = 0;       // resolved again from the names
        }

        bool ok = true;
        for (auto it = values.cbegin(); it != values.cend() && ok; ++it) {
            const QString &value = it.value();
            switch (it.key()) {
            case Field::Code:        p.code = value; break;
            case Field::Description: p.description = value; break;
            case Field::Brand:       p.brandName = value; break;
            case Field::Category:    p.categoryName = value; break;
            case Field::Price:       ok = AguaraMoney::parse(value, &p.price); break;
            case Field::Cost:        ok = AguaraMoney::parse(value, &p.cost); break;
            case Field::Stock:       p.stock = value.toInt(&ok); break;
            case Field::StockMin:    p.stockMin = value.toInt(&ok); break;
            case Field::Vat:         p.vatRate = QString(value).remove('%').toInt(&ok); break;
            case Field::Purchased:   p.purchased = parseDate(value); ok = p.purchased.isValid(); break;
            case Field::Ignored:     break;
            }
            if (!ok)
                problems << tr("línea %1: valor inválido \"%2\"").arg(lineNumber).arg(value);
        }
        if (ok && (p.code.isEmpty() || p.description.isEmpty())) {
            problems << tr("línea %1: falta código o descripción").arg(lineNumber);
            ok = false;
        }
        if (ok)
            parsed.append(p);
    }

    int saved = 0;
    if (!products->importProducts(parsed, &saved)) {
        report(tr("Importación fallida"),
               tr("%1\nNo se importó ningún artículo.").arg(products->lastError()),
               MsgPriority::PriorityHigh);
        return false;
    }

    QString summary = tr("%1 artículos importados desde %2.").arg(saved).arg(QFileInfo(filename).fileName());
    if (!problems.isEmpty()) {
        summary += tr("\n%1 líneas omitidas:\n").arg(problems.size())
                 + QStringList(problems.mid(0, 5)).join('\n')
                 + (problems.size() > 5 ? QStringLiteral("\n...") : QString());
    }
    report(tr("Importación terminada"), summary,
           problems.isEmpty() ? MsgPriority::PriorityNormal : MsgPriority::PriorityHigh);
    return true;
}
