/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#include "pm_upy/lib/lwip.h"
#include "pm_common.h"
#include "py/mpconfig.h"

#ifndef MICROPY_PY_LWIP
#define MICROPY_PY_LWIP 0
#endif

#if MICROPY_PY_LWIP
#include "extmod/modnetwork.h"
#endif

int pm_upy_lwip_available(void) {
    return MICROPY_PY_LWIP ? 1 : 0;
}

int pm_upy_lwip_init(void) {
#if MICROPY_PY_LWIP
    mod_network_lwip_init();
    return PM_OK;
#else
    return PM_ERR_FEATURE;
#endif
}

int pm_upy_lwip_poll(uint32_t ticks_ms) {
#if MICROPY_PY_LWIP
    mod_network_lwip_poll_wrapper(ticks_ms);
    return PM_OK;
#else
    (void)ticks_ms;
    return PM_ERR_FEATURE;
#endif
}
