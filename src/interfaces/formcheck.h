#ifndef FORMCHECK_H
#define FORMCHECK_H

#include "aguara_global.h"
#include "quantity.h"

#include <QString>

class QDateEdit;
class QLabel;
class QWidget;

/// What every form shares: how a required field looks, how a problem is shown,
/// what a phone or an e-mail may look like, and how removing something is asked.
namespace AguaraForm {

/// "Nombre:" becomes "Nombre *:". The forms mark what they can't save without.
AGUARA_EXPORT QString required(const QString &label);
/// The line that explains the mark.
AGUARA_EXPORT QLabel *requiredNote();

/// Hidden until there is something to say. It goes above the form's buttons.
AGUARA_EXPORT QLabel *errorLabel(const QString &objectName);
/// Shows the problem and, given the field it is about, takes the cursor there.
AGUARA_EXPORT void showError(QLabel *label, const QString &text, QWidget *field = nullptr);

// The optional data: empty is valid, what is written has to make sense.
AGUARA_EXPORT bool validPhone(const QString &phone);
AGUARA_EXPORT bool validEmail(const QString &email);
AGUARA_EXPORT bool validWeb(const QString &web);

/// Signing in trims the name, so a name can't have spaces in it.
AGUARA_EXPORT bool validUsername(const QString &username);
/// An article's code or barcode, as the forms and a scanner write it.
AGUARA_EXPORT bool validCode(const QString &code);
/// A kilo takes 1,250, but a unit with no decimals doesn't take 1,5.
AGUARA_EXPORT bool fitsUnit(Quantity quantity, int decimals);

/// Asks before removing or deactivating something. Enter and Esc keep it:
/// removing takes Tab and Enter, or the mouse. A plain dialog, not a QMessageBox,
/// which can't be shown on the offscreen platform the tests run on.
AGUARA_EXPORT bool confirmRemoval(QWidget *parent, const QString &title, const QString &text,
                                  const QString &action, const QString &keep = QString());

/// "Desde" never ends up after "Hasta": moving one brings the other along.
AGUARA_EXPORT void keepInOrder(QDateEdit *from, QDateEdit *to);

} // namespace AguaraForm

#endif // FORMCHECK_H
