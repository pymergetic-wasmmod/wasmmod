/*
 * Compiler scope node allocation.
 */

#include "pm_upy/util/scope.h"
#include "py/mpconfig.h"

#ifndef MICROPY_ENABLE_COMPILER
#define MICROPY_ENABLE_COMPILER 0
#endif

#if MICROPY_ENABLE_COMPILER
#include "py/parse.h"
#include "py/scope.h"
#endif

void *pm_upy_scope_new(void) {
#if MICROPY_ENABLE_COMPILER
    return scope_new(SCOPE_MODULE, MP_PARSE_NODE_NULL, 0);
#else
    return NULL;
#endif
}
