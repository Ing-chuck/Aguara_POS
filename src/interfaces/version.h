#ifndef VERSION_H
#define VERSION_H

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

#define VERSION_MAJOR 0
#define VERSION_MINOR 0
#define VERSION_PATCH 14

#define VERSION_STRING "v" TOSTRING(VERSION_MAJOR) "." TOSTRING(VERSION_MINOR)// "." TOSTRING(VERSION_PATCH)
#endif // VERSION_H
