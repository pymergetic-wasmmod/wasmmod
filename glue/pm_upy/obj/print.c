/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#include <stdarg.h>

#include "pm_upy/obj/print.h"
#include "py/mpprint.h"

void pm_upy_printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    mp_vprintf(&mp_plat_print, fmt, ap);
    va_end(ap);
}
