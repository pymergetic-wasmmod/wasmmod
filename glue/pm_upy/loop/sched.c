/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#include "pm_upy/loop/sched.h"
#include "pm_common.h"
#include "py/runtime.h"

#ifndef MICROPY_ENABLE_SCHEDULER
#define MICROPY_ENABLE_SCHEDULER 0
#endif

int pm_upy_sched_schedule(void *fun, void *arg) {
#if MICROPY_ENABLE_SCHEDULER
    if (!mp_sched_schedule((mp_obj_t)(uintptr_t)fun, (mp_obj_t)(uintptr_t)arg)) {
        return PM_ERR;
    }
    return PM_OK;
#else
    (void)fun;
    (void)arg;
    return PM_ERR_FEATURE;
#endif
}

int pm_upy_sched_num_pending(void) {
#if MICROPY_ENABLE_SCHEDULER
    return (int)mp_sched_num_pending();
#else
    return 0;
#endif
}

void pm_upy_sched_lock(void) {
#if MICROPY_ENABLE_SCHEDULER
    mp_sched_lock();
#endif
}

void pm_upy_sched_unlock(void) {
#if MICROPY_ENABLE_SCHEDULER
    mp_sched_unlock();
#endif
}

void pm_upy_sched_keyboard_interrupt(void) {
    mp_sched_keyboard_interrupt();
}
