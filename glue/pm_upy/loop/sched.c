/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#include "pm_upy/loop/sched.h"
#include "pm_common.h"
#include "py/mphal.h"
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

int pm_upy_sched_exception(void *exc) {
#if MICROPY_ENABLE_SCHEDULER
    if (exc == NULL) {
        return PM_ERR_ARG;
    }
    mp_sched_exception((mp_obj_t)(uintptr_t)exc);
    return PM_OK;
#else
    (void)exc;
    return PM_ERR_FEATURE;
#endif
}

int pm_upy_event_wait_ms(uint32_t ms) {
#if defined(MICROPY_EVENT_POLL_HOOK_FAST)
    (void)ms;
    MICROPY_EVENT_POLL_HOOK_FAST;
    return PM_OK;
#elif defined(MICROPY_EVENT_POLL_HOOK)
    (void)ms;
    MICROPY_EVENT_POLL_HOOK;
    return PM_OK;
#else
    mp_hal_delay_ms(ms);
    mp_handle_pending(MP_HANDLE_PENDING_CALLBACKS_AND_EXCEPTIONS);
    return PM_OK;
#endif
}
