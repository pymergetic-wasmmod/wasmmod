/*
 * Format float into a C buffer.
 */

#include "pm_upy/util/formatfloat.h"
#include "pm_common.h"
#include "py/mpconfig.h"

#ifndef MICROPY_FLOAT_IMPL
#define MICROPY_FLOAT_IMPL 0
#endif

#if MICROPY_FLOAT_IMPL
#include "py/formatfloat.h"
#endif

int pm_upy_format_float(double v, char *buf, size_t len) {
#if MICROPY_FLOAT_IMPL
    if (!buf || !len) {
        return PM_ERR_ARG;
    }
    return mp_format_float((mp_float_t)v, buf, len, 'g', 6, '\0');
#else
    (void)v;
    (void)buf;
    (void)len;
    return PM_ERR_FEATURE;
#endif
}
