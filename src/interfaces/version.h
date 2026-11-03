#ifndef VERSION_H
#define VERSION_H

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

#define VERSION_MAJOR 0
#define VERSION_MINOR 0
#define VERSION_PATCH 23

// For display only. Module compatibility is checked by the versioned
// interface IDs (interfaces.h), not by this string.
#define VERSION_STRING "v" TOSTRING(VERSION_MAJOR) "." TOSTRING(VERSION_MINOR) "." TOSTRING(VERSION_PATCH)
#endif // VERSION_H
