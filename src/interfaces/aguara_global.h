#ifndef AGUARA_GLOBAL_H
#define AGUARA_GLOBAL_H

#include <QtGlobal>

// The interfaces library is a shared library (DLL), so the core and every
// module share a single copy of its classes and their meta-objects.
// That is what makes qobject_cast work across the exe/plugin boundary.
#if defined(AGUARA_INTERFACES_LIBRARY)
#  define AGUARA_EXPORT Q_DECL_EXPORT
#else
#  define AGUARA_EXPORT Q_DECL_IMPORT
#endif

#endif // AGUARA_GLOBAL_H
