/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_UPY_LOOP_SCHED_H_
#define PM_PM_UPY_LOOP_SCHED_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

int pm_upy_sched_schedule(void *fun, void *arg);
int pm_upy_sched_num_pending(void);
void pm_upy_sched_lock(void);
void pm_upy_sched_unlock(void);
void pm_upy_sched_keyboard_interrupt(void);
int pm_upy_sched_exception(void *exc);
int pm_upy_event_wait_ms(uint32_t ms);

#include "pm_guest.h"
#if PM_WASMMOD_GUEST
#if PM_WASMMOD_GUEST_WASM
/* fun/arg are host object handles. */
MP_WASM_IMPORT("micropython.runtime", int, sched_schedule, int32_t fun_h, int32_t arg_h);
#else
MP_WASM_IMPORT("micropython.runtime", int, sched_schedule, void *fun, void *arg);
#endif
#endif

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_UPY_LOOP_SCHED_H_ */
