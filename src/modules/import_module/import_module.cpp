#include "import_module.h"
#include "csvreader.h"
#include <formcheck.h>
#include <version.h>

#include <QDate>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMap>
#include <QMultiHash>
#include <QSet>

#include <algorithm>

namespace {

/// Product fields a CSV column can map to
enum class Field { Code, Description, Brand, Category, Price, Cost, Stock, StockMin, Vat, Purchased, Expires, Ignored };
inline uint qHash(Field f, uint seed = 0) { return ::qHash(int(f), seed); }

/// Customer and supplier fields a CSV column can map to
enum class Contact { Name, Document, DocumentType, Ruc, Ci, Address, Phone, Email, Person, Web, Notes };
inline uint qHash(Contact f, uint seed = 0) { return ::qHash(int(f), seed); }
using ContactValues = QHash<Contact, QString>;

/// One line of a file of customers or suppliers, with what it says.
struct ContactRow {
    int line = 0;
    ContactValues values;
};

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
        { "vencimiento", Field::Expires },   { "fvencimiento", Field::Expires },
        { "fechavencimiento", Field::Expires }, { "fechadevencimiento", Field::Expires },
    };
    return fields.value(normalizeHeader(header), Field::Ignored);
}

const QHash<QString, Contact> &customerColumns()
{
    static const QHash<QString, Contact> columns = {
        { "nombre", Contact::Name },            { "cliente", Contact::Name },
        { "razonsocial", Contact::Name },       { "documento", Contact::Document },
        { "tipodocumento", Contact::DocumentType }, { "tipo", Contact::DocumentType },
        { "ruc", Contact::Ruc },                { "cedula", Contact::Ci },
        { "ci", Contact::Ci },                  { "direccion", Contact::Address },
        { "telefono", Contact::Phone },         { "celular", Contact::Phone },
        { "correo", Contact::Email },           { "email", Contact::Email },
        { "correoelectronico", Contact::Email },
    };
    return columns;
}

const QHash<QString, Contact> &supplierColumns()
{
    static const QHash<QString, Contact> columns = {
        { "nombre", Contact::Name },            { "proveedor", Contact::Name },
        { "razonsocial", Contact::Name },       { "direccion", Contact::Address },
        { "telefono", Contact::Phone },         { "celular", Contact::Phone },
        { "correo", Contact::Email },           { "email", Contact::Email },
        { "correoelectronico", Contact::Email },{ "contacto", Contact::Person },
        { "web", Contact::Web },                { "sitioweb", Contact::Web },
        { "notas", Contact::Notes },            { "observaciones", Contact::Notes },
    };
    return columns;
}

/// The rows below the first one that has a name column: a title above it is
/// skipped, and no such row (*found false) means this isn't that kind of file.
/// With two columns for one field, as Cliente and Razon Social, the first with
/// something written wins.
QList<ContactRow> contactRows(const QString &text, const QHash<QString, Contact> &names, bool *found)
{
    QMap<int, Contact> columns;
    QList<ContactRow> rows;
    for (const Csv::Record &record : Csv::parse(text, Csv::detectSeparator(text))) {
        QStringList cols = record.fields;
        for (QString &col : cols)
            col = col.trimmed();
        if (cols.join(QString()).isEmpty())
            continue;

        if (columns.isEmpty()) {
            QMap<int, Contact> header;
            for (int i = 0; i < cols.size(); ++i) {
                const auto name = names.constFind(normalizeHeader(cols.at(i)));
                if (name != names.constEnd())
                    header.insert(i, name.value());
            }
            if (std::find(header.cbegin(), header.cend(), Contact::Name) != header.cend())
                columns = header;
            continue;
        }

        ContactRow row;
        row.line = record.line;
        for (auto it = columns.cbegin(); it != columns.cend(); ++it) {
            const QString value = it.key() < cols.size() ? cols.at(it.key()) : QString();
            if (!value.isEmpty() && value != QLatin1String("NULL") && !row.values.contains(it.value()))
                row.values.insert(it.value(), value);
        }
        if (!row.values.isEmpty())
            rows << row;
    }
    *found = !columns.isEmpty();
    return rows;
}

/// A customer's document as the file has it: its own column for each kind, or
/// one Documento column, with a Tipo column or else by its shape. "80.009.735-1"
/// comes as "80009735-1". False if the kind written isn't one that exists.
bool documentOf(const ContactValues &values, QString *number, QString *type)
{
    number->clear();
    type->clear();
    if (values.contains(Contact::Ruc)) {
        *number = values.value(Contact::Ruc);
        *type = QLatin1String(DocumentType::Ruc);
    } else if (values.contains(Contact::Ci)) {
        *number = values.value(Contact::Ci);
        *type = QLatin1String(DocumentType::Ci);
    } else if (values.contains(Contact::Document)) {
        *number = values.value(Contact::Document);
        const QString kind = normalizeHeader(values.value(Contact::DocumentType));
        if (kind.isEmpty())
            *type = QLatin1String(number->contains(QLatin1Char('-')) ? DocumentType::Ruc : DocumentType::Ci);
        else if (kind == QLatin1String("ruc"))
            *type = QLatin1String(DocumentType::Ruc);
        else if (kind == QLatin1String("ci") || kind == QLatin1String("cedula")
                 || kind == QLatin1String("ceduladeidentidad"))
            *type = QLatin1String(DocumentType::Ci);
        else if (kind == QLatin1String("sindocumento"))
            number->clear();
        else
            return false;
    }
    number->remove(QLatin1Char('.'));
    number->remove(QLatin1Char(' '));
    *number = number->toUpper();
    return true;
}

/// What saving would refuse. Checked on the record as it would end up, here,
/// because for the service one bad line would stop the whole file.
QString customerProblem(const Customer &c)
{
    if (c.documentType == QLatin1String(DocumentType::Ruc) && !AguaraCustomer::validRuc(c.document))
        return ImportModule::tr("el RUC \"%1\" no es válido").arg(c.document);
    if (c.documentType == QLatin1String(DocumentType::Ci) && !AguaraCustomer::validCi(c.document))
        return ImportModule::tr("la cédula \"%1\" no es válida").arg(c.document);
    if (!AguaraForm::validPhone(c.phone))
        return ImportModule::tr("el teléfono \"%1\" no es válido").arg(c.phone);
    if (!AguaraForm::validEmail(c.email))
        return ImportModule::tr("el correo \"%1\" no es válido").arg(c.email);
    return QString();
}

QString supplierProblem(const Supplier &s)
{
    if (!AguaraForm::validPhone(s.phone))
        return ImportModule::tr("el teléfono \"%1\" no es válido").arg(s.phone);
    if (!AguaraForm::validEmail(s.email))
        return ImportModule::tr("el correo \"%1\" no es válido").arg(s.email);
    if (!AguaraForm::validWeb(s.web))
        return ImportModule::tr("la web \"%1\" no es válida").arg(s.web);
    return QString();
}

/// Names are compared without case or doubled spaces.
QString nameKey(const QString &name)
{
    return name.simplified().toLower();
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
    info.description = "Importa artículos, clientes y proveedores desde archivos CSV (por ejemplo, exportados de Excel)";
    info.version     = VERSION_STRING;
    info.iconName    = "import";    // theme icon (AguaraStyle::icon)
    return info;
}

bool ImportModule::initialize(AguaraHost *h)
{
    host = h;
    products = host->service<ProductService>();
    customers = host->service<CustomerService>();
    inventory = host->service<InventoryService>();
    return products != nullptr;
}

/// File types this importer can read
QStringList ImportModule::fileTypes() const {
    return { "csv" };
}

bool ImportModule::canImport(ImportKind kind) const
{
    switch (kind) {
    case ImportKind::Articles:  return products != nullptr;
    case ImportKind::Customers: return customers != nullptr;
    case ImportKind::Suppliers: return inventory != nullptr;
    }
    return false;
}

bool ImportModule::importFrom(const QString &fileName, ImportKind kind)
{
    switch (kind) {
    case ImportKind::Articles:  return importArticles(fileName);
    case ImportKind::Customers: return importCustomers(fileName);
    case ImportKind::Suppliers: return importSuppliers(fileName);
    }
    return false;
}

bool ImportModule::readFile(const QString &fileName, QString *text)
{
    QFile csv(fileName);
    if (!csv.open(QIODevice::ReadOnly))
        return failed(tr("No se pudo abrir %1").arg(fileName));
    *text = Csv::decode(csv.readAll());
    return true;
}

bool ImportModule::failed(const QString &text)
{
    report(tr("Importación fallida"), text, MsgPriority::PriorityHigh);
    return false;
}

void ImportModule::finish(QString summary, const QStringList &problems)
{
    if (!problems.isEmpty()) {
        summary += tr("\n%1 líneas omitidas:\n").arg(problems.size())
                 + QStringList(problems.mid(0, 5)).join('\n')
                 + (problems.size() > 5 ? QStringLiteral("\n...") : QString());
    }
    report(tr("Importación terminada"), summary,
           problems.isEmpty() ? MsgPriority::PriorityNormal : MsgPriority::PriorityHigh);
}

void ImportModule::report(const QString &title, const QString &text, MsgPriority priority)
{
    host->notify(ModuleMsg(moduleInfo().name, "import", title, text, priority));
}

/// Read a CSV file of articles and save them through the ProductService.
/// The first row whose first cell isn't a number is taken as the header row;
/// columns are matched by name (Codigo, Descripcion, Marca, Rubro,
/// PrecioVenta/Precio, Costo, Stock, StockMin, IVA, F_Compra, Vencimiento), others ignored.
/// The separator (',' or ';') is detected from the first line. Quoted fields
/// ("..." with "" for a quote) may contain the separator and line breaks.
/// Encoding: UTF-8, or Windows-1252 (Excel on Spanish Windows).
/// All rows are saved in one transaction: a database error saves nothing.
bool ImportModule::importArticles(const QString &filename)
{
    QString text;
    if (!readFile(filename, &text))
        return false;

    QSet<int> rates;
    for (const VatCategory &vat : products->vatCategories())
        rates.insert(vat.rate);

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
            case Field::Stock:       ok = AguaraQuantity::parse(value, &p.stock); break;
            case Field::StockMin:    ok = AguaraQuantity::parse(value, &p.stockMin); break;
            case Field::Vat:         p.vatRate = QString(value).remove('%').toInt(&ok); break;
            case Field::Purchased:   p.purchased = parseDate(value); ok = p.purchased.isValid(); break;
            case Field::Expires:     p.stockExpires = parseDate(value); ok = p.stockExpires.isValid(); break;
            case Field::Ignored:     break;
            }
            if (!ok)
                problems << tr("línea %1: valor inválido \"%2\"").arg(lineNumber).arg(value);
        }
        if (ok && (p.code.isEmpty() || p.description.isEmpty())) {
            problems << tr("línea %1: falta código o descripción").arg(lineNumber);
            ok = false;
        }
        // one bad line is left out; for the service, it would stop the whole file
        if (ok && (p.price < 0 || p.cost < 0 || p.stockMin < 0)) {
            problems << tr("línea %1: precio, costo o stock mínimo negativo").arg(lineNumber);
            ok = false;
        }
        if (ok && !rates.contains(p.vatRate)) {
            problems << tr("línea %1: la tasa de IVA %2% no existe").arg(lineNumber).arg(p.vatRate);
            ok = false;
        }
        if (ok)
            parsed.append(p);
    }

    int saved = 0;
    if (!products->importProducts(parsed, &saved))
        return failed(tr("%1\nNo se importó ningún artículo.").arg(products->lastError()));

    finish(tr("%1 artículos importados desde %2.").arg(saved).arg(QFileInfo(filename).fileName()), problems);
    return true;
}

/// Read a CSV file of customers. Columns by name, in any order: Nombre (or
/// Cliente, Razon Social), RUC, Cedula (or CI), Documento with Tipo Documento,
/// Direccion, Telefono (or Celular), Correo (or Email). Others are ignored.
///
/// A line with a document updates the customer with that document, and a line
/// without one the only active customer of that name; a name that several share
/// is left out, not guessed. A document never goes to someone by name alone:
/// a wrong one would end up on invoices. What a line leaves empty is kept.
/// A customer who left stays inactive: an import only changes data.
bool ImportModule::importCustomers(const QString &filename)
{
    QString text;
    if (!readFile(filename, &text))
        return false;
    bool hasHeader = false;
    const QList<ContactRow> rows = contactRows(text, customerColumns(), &hasHeader);
    if (!hasHeader)
        return failed(tr("%1 no tiene una columna Nombre: no parece un archivo de clientes.")
                          .arg(QFileInfo(filename).fileName()));
    const std::optional<QList<Customer>> existing = customers->allCustomers();
    if (!existing)
        return failed(tr("%1\nNo se importó ningún cliente.").arg(customers->lastError()));

    QList<Customer> known = *existing;      // the ones in the file join it: a line can repeat one
    QHash<QString, int> byDocument;
    QMultiHash<QString, int> byName;
    for (int i = 0; i < known.size(); ++i) {
        if (!known.at(i).document.isEmpty())
            byDocument.insert(known.at(i).document, i);
        if (known.at(i).active)
            byName.insert(nameKey(known.at(i).name), i);
    }

    QSet<int> touched;
    QStringList problems;
    for (const ContactRow &row : rows) {
        QString number, type;
        if (!documentOf(row.values, &number, &type)) {
            problems << tr("línea %1: tipo de documento \"%2\" desconocido")
                            .arg(row.line).arg(row.values.value(Contact::DocumentType));
            continue;
        }
        const QString name = row.values.value(Contact::Name).simplified();
        int at = -1;
        if (!number.isEmpty()) {
            at = byDocument.value(number, -1);
        } else if (!name.isEmpty()) {
            const QList<int> same = byName.values(nameKey(name));
            if (same.size() > 1) {
                problems << tr("línea %1: hay varios clientes llamados %2, falta el documento").arg(row.line).arg(name);
                continue;
            }
            if (!same.isEmpty())
                at = same.first();
        }
        if (at < 0 && name.isEmpty()) {
            problems << tr("línea %1: falta el nombre").arg(row.line);
            continue;
        }

        Customer c = at >= 0 ? known.at(at) : Customer();
        if (!name.isEmpty())
            c.name = name;
        if (!number.isEmpty()) {
            c.document = number;
            c.documentType = type;
        }
        if (row.values.contains(Contact::Address))
            c.address = row.values.value(Contact::Address);
        if (row.values.contains(Contact::Phone))
            c.phone = row.values.value(Contact::Phone);
        if (row.values.contains(Contact::Email))
            c.email = row.values.value(Contact::Email);
        const QString problem = customerProblem(c);
        if (!problem.isEmpty()) {
            problems << tr("línea %1: %2").arg(row.line).arg(problem);
            continue;
        }

        if (at >= 0) {
            known[at] = c;
        } else {
            at = known.size();
            known << c;
            byName.insert(nameKey(c.name), at);
            if (!c.document.isEmpty())
                byDocument.insert(c.document, at);
        }
        touched.insert(at);
    }

    QList<int> order = touched.values();
    std::sort(order.begin(), order.end());
    QList<Customer> toSave;
    int created = 0;
    int stillInactive = 0;
    for (int index : qAsConst(order)) {
        toSave << known.at(index);
        created += known.at(index).id == 0 ? 1 : 0;
        stillInactive += known.at(index).active ? 0 : 1;
    }
    int saved = 0;
    if (!customers->importCustomers(toSave, &saved))
        return failed(tr("%1\nNo se importó ningún cliente.").arg(customers->lastError()));

    QString summary = tr("%1 clientes importados desde %2 (nuevos: %3, actualizados: %4).")
                          .arg(saved).arg(QFileInfo(filename).fileName()).arg(created).arg(saved - created);
    if (stillInactive > 0)
        summary += tr("\n%1 estaban dados de baja y siguen así.").arg(stillInactive);
    finish(summary, problems);
    return true;
}

/// Read a CSV file of suppliers. Columns by name, in any order: Nombre (or
/// Proveedor, Razon Social), Direccion, Telefono (or Celular), Correo (or Email),
/// Contacto, Web, Notas (or Observaciones). Others are ignored.
///
/// A supplier has no document, so its name is what identifies it: a line updates
/// the active supplier of that name and keeps what it leaves empty, and any other
/// name is a new one, as in the form.
bool ImportModule::importSuppliers(const QString &filename)
{
    QString text;
    if (!readFile(filename, &text))
        return false;
    bool hasHeader = false;
    const QList<ContactRow> rows = contactRows(text, supplierColumns(), &hasHeader);
    if (!hasHeader)
        return failed(tr("%1 no tiene una columna Nombre: no parece un archivo de proveedores.")
                          .arg(QFileInfo(filename).fileName()));

    QList<Supplier> known = inventory->suppliers();
    QHash<QString, int> byName;
    for (int i = 0; i < known.size(); ++i)
        byName.insert(nameKey(known.at(i).name), i);

    QSet<int> touched;
    QStringList problems;
    for (const ContactRow &row : rows) {
        const QString name = row.values.value(Contact::Name).simplified();
        if (name.isEmpty()) {
            problems << tr("línea %1: falta el nombre").arg(row.line);
            continue;
        }
        int at = byName.value(nameKey(name), -1);
        Supplier s = at >= 0 ? known.at(at) : Supplier();
        s.name = name;
        if (row.values.contains(Contact::Address))
            s.address = row.values.value(Contact::Address);
        if (row.values.contains(Contact::Phone))
            s.phone = row.values.value(Contact::Phone);
        if (row.values.contains(Contact::Email))
            s.email = row.values.value(Contact::Email);
        if (row.values.contains(Contact::Person))
            s.contact = row.values.value(Contact::Person);
        if (row.values.contains(Contact::Web))
            s.web = row.values.value(Contact::Web);
        if (row.values.contains(Contact::Notes))
            s.notes = row.values.value(Contact::Notes);
        const QString problem = supplierProblem(s);
        if (!problem.isEmpty()) {
            problems << tr("línea %1: %2").arg(row.line).arg(problem);
            continue;
        }

        if (at >= 0) {
            known[at] = s;
        } else {
            at = known.size();
            known << s;
            byName.insert(nameKey(name), at);
        }
        touched.insert(at);
    }

    QList<int> order = touched.values();
    std::sort(order.begin(), order.end());
    QList<Supplier> toSave;
    int created = 0;
    for (int index : qAsConst(order)) {
        toSave << known.at(index);
        created += known.at(index).id == 0 ? 1 : 0;
    }
    int saved = 0;
    if (!inventory->importSuppliers(toSave, &saved))
        return failed(tr("%1\nNo se importó ningún proveedor.").arg(inventory->lastError()));

    finish(tr("%1 proveedores importados desde %2 (nuevos: %3, actualizados: %4).")
               .arg(saved).arg(QFileInfo(filename).fileName()).arg(created).arg(saved - created), problems);
    return true;
}
