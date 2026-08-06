/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_UPY_EXEC_NATIVE_H_
#define PM_PM_UPY_EXEC_NATIVE_H_

#ifdef __cplusplus
extern "C" {
#endif

int pm_upy_dynruntime_available(void);

#include <stddef.h>
#include <stdint.h>
int pm_upy_dynruntime_load(const uint8_t *mpy, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_UPY_EXEC_NATIVE_H_ */
