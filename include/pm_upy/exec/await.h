/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_UPY_EXEC_AWAIT_H_
#define PM_PM_UPY_EXEC_AWAIT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
uint32_t pm_upy_await(uint32_t self_h, uint32_t child_h);
uint32_t pm_upy_sleep_us(uint64_t us);
uint32_t pm_upy_new_awaitable(uint32_t handle);
int pm_upy_resume(void *obj);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_UPY_EXEC_AWAIT_H_ */
