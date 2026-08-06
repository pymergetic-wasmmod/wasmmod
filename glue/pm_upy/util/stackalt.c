/*
 * Optional Python stack allocator init.
 */

#include "pm_upy/util/stackalt.h"
#include "pm_common.h"
#include "py/mpconfig.h"

#ifndef MICROPY_ENABLE_PYSTACK
#define MICROPY_ENABLE_PYSTACK 0
#endif

#if MICROPY_ENABLE_PYSTACK
#include "py/pystack.h"
#endif

int pm_upy_pystack_init(void *start, void *end) {
#if MICROPY_ENABLE_PYSTACK
    if (!start || !end || start >= end) {
        return PM_ERR_ARG;
    }
    mp_pystack_init(start, end);
    return PM_OK;
#else
    (void)start;
    (void)end;
    return PM_ERR_FEATURE;
#endif
}
