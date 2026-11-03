#ifndef AGUARASTYLE_H
#define AGUARASTYLE_H

#include "aguara_global.h"

#include <QColor>
#include <QIcon>
#include <QString>

class QApplication;
class QWidget;

/*
    The look of AguaraPOS, shared by the core and every module.

    The core applies it once (apply()). Modules don't style widgets
    themselves: they give a widget a *role* and the theme does the rest, so
    every page looks the same and a theme change reaches all modules.

    Roles (dynamic property "role", also settable in Qt Designer):

      QPushButton / QToolButton
        "primary"    the main action of a page or dialog (one per screen).
                     A dialog's default button gets this look automatically.
        "danger"     destructive actions (dar de baja, borrar)
        "flat"       no frame: icon buttons, secondary links
      QLabel
        "title"      page or dialog title        "subtitle"  line under a title
        "muted"      secondary information       "section"   small heading in a form
        "error" / "warning" / "info" / "success"  a message banner
      QWidget / QFrame
        "pageHeader" the band at the top of a page (title + actions)
        "card"       a white box with a border
      QLineEdit
        "search"     a search box (use searchField() to add the icon)

    Icons: icon("search") etc. are line icons drawn in one color, so they
    match the text around them. Names: see icons/ in the resources.
*/
namespace AguaraStyle {

// Role names: AguaraStyle::setRole(button, AguaraStyle::Primary)
constexpr const char *Primary    = "primary";
constexpr const char *Danger     = "danger";
constexpr const char *Flat       = "flat";
constexpr const char *Title      = "title";
constexpr const char *Subtitle   = "subtitle";
constexpr const char *Muted      = "muted";
constexpr const char *Section    = "section";
constexpr const char *Error      = "error";
constexpr const char *Warning    = "warning";
constexpr const char *Info       = "info";
constexpr const char *Success    = "success";
constexpr const char *PageHeader = "pageHeader";
constexpr const char *Card       = "card";
constexpr const char *Search     = "search";

/// The palette. Use these instead of literal colors.
enum class Color {
    Window,         ///< page background
    Surface,        ///< cards, tables, inputs
    SurfaceAlt,     ///< alternate table rows, headers
    Border,
    BorderStrong,
    Text,
    TextMuted,
    TextDisabled,
    Accent,         ///< primary actions, selection, focus
    AccentHover,
    AccentPressed,
    AccentSoft,     ///< selected rows, highlights (dark text on it)
    OnAccent,       ///< text on Accent
    Sidebar,
    SidebarHover,
    SidebarActive,
    SidebarText,
    SidebarMuted,
    SidebarAccent,  ///< marker of the active page in the sidebar
    Danger, DangerSoft,
    Warning, WarningSoft,
    Info, InfoSoft,
    Success, SuccessSoft,
    Neutral, NeutralSoft,
};

AGUARA_EXPORT QColor color(Color c);

/// Install the theme on the application: style, palette, font, style sheet.
/// Called once by the core, before any window is created.
AGUARA_EXPORT void apply(QApplication *app);

/// Give `widget` a role (see above) and re-apply the style to it.
AGUARA_EXPORT void setRole(QWidget *widget, const char *role);

/// A line icon in `color` (default: Text). Unknown names give a null icon.
AGUARA_EXPORT QIcon icon(const QString &name, const QColor &color = QColor());

/// The AguaraPOS logo.
AGUARA_EXPORT QIcon logo();

/// Turn a QLineEdit into a search box: role "search", a magnifier inside,
/// and a clear button.
AGUARA_EXPORT void searchField(QWidget *lineEdit);

} // namespace AguaraStyle

#endif // AGUARASTYLE_H
