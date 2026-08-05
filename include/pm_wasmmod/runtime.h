/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_WASMMOD_RUNTIME_H_
#define PM_PM_WASMMOD_RUNTIME_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "pm_common.h"

bool pm_wasmmod_runtime_init(void);
void pm_wasmmod_runtime_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_WASMMOD_RUNTIME_H_ */
