/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#include "pm_upy/loop/step.h"
#include "pm_common.h"
#include "py/runtime.h"
#include "shared/runtime/pyexec.h"

#ifndef MICROPY_HELPER_REPL
#define MICROPY_HELPER_REPL 0
#endif

int pm_upy_handle_pending(void) {
    mp_handle_pending(MP_HANDLE_PENDING_CALLBACKS_AND_EXCEPTIONS);
    return PM_OK;
}

int pm_upy_loop_step(void) {
    return pm_upy_handle_pending();
}

int pm_upy_loop_feed(const uint8_t *ptr, size_t len) {
#if MICROPY_HELPER_REPL
    if (!ptr) {
        return PM_ERR_ARG;
    }
    for (size_t i = 0; i < len; i++) {
        int r = pyexec_event_repl_process_char((int)ptr[i]);
        if (r & PYEXEC_FORCED_EXIT) {
            return r;
        }
    }
    return PM_OK;
#else
    (void)ptr;
    (void)len;
    return PM_ERR_FEATURE;
#endif
}

int pm_upy_loop_reset(void) {
#if MICROPY_HELPER_REPL
    pyexec_event_repl_init();
    return PM_OK;
#else
    return PM_ERR_FEATURE;
#endif
}
