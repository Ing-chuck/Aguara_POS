#include "aguarastyle.h"

#include <QApplication>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QHash>
#include <QLineEdit>
#include <QPainter>
#include <QPalette>
#include <QProxyStyle>
#include <QStyle>
#include <QStyleFactory>
#include <QSvgRenderer>
#include <QWidget>

namespace AguaraStyle {

namespace {

struct Token { Color color; const char *name; const char *value; };

// Light theme. Contrast: Text on Window 13:1, OnAccent on Accent 5.2:1,
// TextMuted on Surface 5.4:1 (all above WCAG AA 4.5:1 for normal text).
const Token kTokens[] = {
    { Color::Window,        "window",        "#F3F4F6" },
    { Color::Surface,       "surface",       "#FFFFFF" },
    { Color::SurfaceAlt,    "surfaceAlt",    "#F8F9FB" },
    { Color::Border,        "border",        "#DADEE4" },
    { Color::BorderStrong,  "borderStrong",  "#B8C0CA" },
    { Color::Text,          "text",          "#1F2933" },
    { Color::TextMuted,     "muted",         "#5B6776" },
    { Color::TextDisabled,  "disabled",      "#9AA4B0" },
    { Color::Accent,        "accent",        "#B94A0B" },   // aguará rust
    { Color::AccentHover,   "accentHover",   "#A2410A" },
    { Color::AccentPressed, "accentPressed", "#873608" },
    { Color::AccentSoft,    "accentSoft",    "#FBEADF" },
    { Color::OnAccent,      "onAccent",      "#FFFFFF" },
    { Color::Sidebar,       "sidebar",       "#1F2933" },
    { Color::SidebarHover,  "sidebarHover",  "#2A3846" },
    { Color::SidebarActive, "sidebarActive", "#344557" },
    { Color::SidebarText,   "sidebarText",   "#D5DDE6" },
    { Color::SidebarMuted,  "sidebarMuted",  "#8D9BAA" },
    { Color::SidebarAccent, "sidebarAccent", "#F08A3E" },
    { Color::Danger,        "danger",        "#B42318" },
    { Color::DangerSoft,    "dangerSoft",    "#FEF1F0" },
    { Color::Warning,       "warning",       "#B54708" },
    { Color::WarningSoft,   "warningSoft",   "#FFF6E5" },
    { Color::Info,          "info",          "#1D5FC2" },
    { Color::InfoSoft,      "infoSoft",      "#EEF4FE" },
    { Color::Success,       "success",       "#127A3E" },
    { Color::SuccessSoft,   "successSoft",   "#ECF8F0" },
    { Color::Neutral,       "neutral",       "#4B5563" },
    { Color::NeutralSoft,   "neutralSoft",   "#F2F3F5" },
};

/// Fusion, with the Windows habit of hiding the underlined menu letters
/// (Alt+letter still works).
class AguaraProxyStyle : public QProxyStyle
{
public:
    using QProxyStyle::QProxyStyle;
    int styleHint(StyleHint hint, const QStyleOption *option, const QWidget *widget,
                  QStyleHintReturn *returnData) const override
    {
        if (hint == SH_UnderlineShortcut)
            return 0;
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }
};

QString styleSheet()
{
    QFile file(QStringLiteral(":/aguara/style/aguara.qss"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    QString qss = QString::fromUtf8(file.readAll());
    for (const Token &t : kTokens)
        qss.replace(QStringLiteral("{{%1}}").arg(QLatin1String(t.name)), QLatin1String(t.value));
    return qss;
}

QPalette palette()
{
    QPalette p;
    p.setColor(QPalette::Window,          color(Color::Window));
    p.setColor(QPalette::WindowText,      color(Color::Text));
    p.setColor(QPalette::Base,            color(Color::Surface));
    p.setColor(QPalette::AlternateBase,   color(Color::SurfaceAlt));
    p.setColor(QPalette::Text,            color(Color::Text));
    p.setColor(QPalette::Button,          color(Color::Surface));
    p.setColor(QPalette::ButtonText,      color(Color::Text));
    p.setColor(QPalette::BrightText,      color(Color::OnAccent));
    p.setColor(QPalette::Highlight,       color(Color::AccentSoft));
    p.setColor(QPalette::HighlightedText, color(Color::Text));
    p.setColor(QPalette::ToolTipBase,     color(Color::Text));
    p.setColor(QPalette::ToolTipText,     color(Color::Surface));
    p.setColor(QPalette::PlaceholderText, color(Color::TextDisabled));
    p.setColor(QPalette::Link,            color(Color::Accent));
    p.setColor(QPalette::Light,           color(Color::Surface));
    p.setColor(QPalette::Midlight,        color(Color::Border));
    p.setColor(QPalette::Mid,             color(Color::BorderStrong));
    p.setColor(QPalette::Dark,            color(Color::TextMuted));
    p.setColor(QPalette::Shadow,          color(Color::Text));
    for (QPalette::ColorRole role : { QPalette::WindowText, QPalette::Text, QPalette::ButtonText })
        p.setColor(QPalette::Disabled, role, color(Color::TextDisabled));
    p.setColor(QPalette::Disabled, QPalette::Base, color(Color::Window));
    p.setColor(QPalette::Disabled, QPalette::Button, color(Color::Window));
    return p;
}

QIcon renderIcon(const QString &file, const QColor &stroke, const QColor &disabled)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return QIcon();
    const QByteArray svg = f.readAll();
    QIcon result;
    auto add = [&](const QColor &c, QIcon::Mode mode) {
        QByteArray colored = svg;
        colored.replace("currentColor", c.name().toLatin1());
        QSvgRenderer renderer(colored);
        for (int size : { 16, 20, 24, 32, 40, 48, 64 }) {
            QPixmap pixmap(size, size);
            pixmap.fill(Qt::transparent);
            QPainter painter(&pixmap);
            painter.setRenderHint(QPainter::Antialiasing);
            renderer.render(&painter);
            painter.end();
            result.addPixmap(pixmap, mode);
        }
    };
    add(stroke, QIcon::Normal);
    if (disabled.isValid())
        add(disabled, QIcon::Disabled);
    return result;
}

} // namespace

QColor color(Color c)
{
    for (const Token &t : kTokens) {
        if (t.color == c)
            return QColor(QLatin1String(t.value));
    }
    return QColor();
}

void apply(QApplication *app)
{
    // Fusion: drawn by Qt, so it looks the same on Windows 7, 10 and 11.
    app->setStyle(new AguaraProxyStyle(QStyleFactory::create(QStringLiteral("Fusion"))));
    app->setPalette(palette());

    // Segoe UI ships with Windows Vista and later.
    QFont font(QStringLiteral("Segoe UI"));
    if (!QFontDatabase().families().contains(font.family()))
        font = QApplication::font();
    font.setPointSizeF(10);
    font.setHintingPreference(QFont::PreferFullHinting);
    app->setFont(font);

    app->setStyleSheet(styleSheet());
    app->setWindowIcon(logo());
}

void setRole(QWidget *widget, const char *role)
{
    if (!widget)
        return;
    widget->setProperty("role", QString::fromLatin1(role));
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

QIcon icon(const QString &name, const QColor &c)
{
    static QHash<QString, QIcon> cache;
    const QColor stroke = c.isValid() ? c : color(Color::Text);
    const QString key = name + QLatin1Char('|') + stroke.name();
    auto it = cache.constFind(key);
    if (it != cache.constEnd())
        return *it;
    const QIcon result = renderIcon(QStringLiteral(":/aguara/icons/%1.svg").arg(name), stroke,
                                    color(Color::TextDisabled));
    cache.insert(key, result);
    return result;
}

QIcon logo()
{
    static const QIcon result = [] {
        QIcon i;
        QSvgRenderer renderer(QStringLiteral(":/aguara/icons/logo.svg"));
        for (int size : { 16, 20, 24, 32, 40, 48, 64, 128, 256 }) {
            QPixmap pixmap(size, size);
            pixmap.fill(Qt::transparent);
            QPainter painter(&pixmap);
            painter.setRenderHint(QPainter::Antialiasing);
            renderer.render(&painter);
            painter.end();
            i.addPixmap(pixmap);
        }
        return i;
    }();
    return result;
}

void searchField(QWidget *widget)
{
    auto lineEdit = qobject_cast<QLineEdit *>(widget);
    if (!lineEdit)
        return;
    setRole(lineEdit, Search);
    if (!lineEdit->property("aguaraSearchIcon").toBool()) {
        lineEdit->addAction(icon(QStringLiteral("search"), color(Color::TextMuted)), QLineEdit::LeadingPosition);
        lineEdit->setProperty("aguaraSearchIcon", true);
    }
    lineEdit->setClearButtonEnabled(true);
}

} // namespace AguaraStyle
