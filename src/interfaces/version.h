#ifndef VERSION_H
#define VERSION_H

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

#define VERSION_MAJOR 1
#define VERSION_MINOR 0
#define VERSION_PATCH 0
#define VERSION_SUFFIX "-beta.2"

// For display only: change it with autoVersion.py. Module compatibility is the
// API level in apiversion.h, not this string.
#define VERSION_STRING "v" TOSTRING(VERSION_MAJOR) "." TOSTRING(VERSION_MINOR) "." TOSTRING(VERSION_PATCH) VERSION_SUFFIX
#endif // VERSION_H
