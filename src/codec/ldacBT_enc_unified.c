/*
 * Sony LDAC Bluetooth High-Resolution Audio Encoder
 * Unified Windows Source (MSVC / Clang compatible)
 */

#ifdef _MSC_VER
#pragma warning(disable: 4244 4267 4018 4133 4047 4024 4146)
#endif

#define LDAC_BUILD_DLL 1

#ifndef __attribute__
#define __attribute__(x)
#endif

#ifndef _WIN32_UNISTD_H_
#define _WIN32_UNISTD_H_
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <io.h>
#endif

/* Include Sony LDAC core */
#include "ldaclib.c"
#include "ldacBT.c"
#include "../../ldacBT_src/libldac/abr/src/ldacBT_abr.c"
