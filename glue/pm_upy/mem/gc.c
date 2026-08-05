/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */


#include "pm_upy/mem/gc.h"
#include "pm_upy/features.h"
#include "pm_common.h"
#if MICROPY_ENABLE_GC
#include "py/gc.h"
#endif

int pm_upy_gc_enabled(void) {
    return pm_upy_has(PM_UPY_FEAT_GC) ? 1 : 0;
}
int pm_upy_gc_collect(void) {
    if (!pm_upy_has(PM_UPY_FEAT_GC)) {
        return PM_ERR_FEATURE;
    }
#if MICROPY_ENABLE_GC
    gc_collect();
    return PM_OK;
#else
    return PM_ERR_FEATURE;
#endif
}

