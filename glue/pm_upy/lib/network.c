/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#include "pm_upy/lib/network.h"
#include "py/mpconfig.h"

#ifndef MICROPY_PY_NETWORK
#define MICROPY_PY_NETWORK 0
#endif

#if MICROPY_PY_NETWORK
#include "extmod/modnetwork.h"
#endif

int pm_upy_network_available(void) {
    return MICROPY_PY_NETWORK ? 1 : 0;
}

const char *pm_upy_network_hostname(void) {
#if MICROPY_PY_NETWORK
    return mod_network_hostname_data;
#else
    return "";
#endif
}
