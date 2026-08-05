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

int pm_upy_sched_schedule(void *fun, void *arg);
int pm_upy_sched_num_pending(void);
void pm_upy_sched_lock(void);
void pm_upy_sched_unlock(void);
void pm_upy_sched_keyboard_interrupt(void);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_UPY_LOOP_SCHED_H_ */
