// TODO: Remove this header

#ifndef COMMON_H
#define COMMON_H

#include "types.h"

/*
 * BOOLX is a helper macro that evaluates an expression and converts it to
 * either 1 or 0.  It is intentionally named BOOLX instead of BOOL to avoid
 * colliding with the BOOL type used in the Wii U system headers (see
 * coreinit/exception.h).  Using a macro named BOOL would cause the C
 * preprocessor to replace occurrences in system headers, breaking type
 * definitions.  See build logs for details.
 */
#define BOOLX(_expr) ((_expr) ? 1 : 0)

#define ALIGN_UP(x, a) (((x) + ((a) - 1)) & ~((a) - 1))

#define LO_16_BITS(val) (val & 0xFFFF)
#define HI_16_BITS(val) ((val & 0xFFFF0000) >> 16)

#endif
