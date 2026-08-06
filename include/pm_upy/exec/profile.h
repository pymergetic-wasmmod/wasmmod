/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_UPY_EXEC_PROFILE_H_
#define PM_PM_UPY_EXEC_PROFILE_H_

#ifdef __cplusplus
extern "C" {
#endif

int pm_upy_profile_settrace(void *cb);

void pm_upy_prof_instr_tick(void);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_UPY_EXEC_PROFILE_H_ */
