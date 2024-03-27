#ifndef __XI_VERSION_H__
#define __XI_VERSION_H__

#include <ProductVer.h>

#define GETSTR(s) #s
#define STR(s)  GETSTR(s)

#define VERSION_STRING  STR(VER_MAJOR)"."STR(VER_MINOR)"."STR(VER_RELEASE)"."STR(VER_BUILD)

#endif /* __XI_VERSION_H__ */

