/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_UPY_OBJ_BINARY_H_
#define PM_PM_UPY_OBJ_BINARY_H_

#ifdef __cplusplus
extern "C" {
#endif

int pm_upy_binary_available(void);

#include <stdint.h>
int pm_upy_binary_get(int typecode, const void *p, int64_t *out);
int pm_upy_binary_set(int typecode, void *p, int64_t val);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_UPY_OBJ_BINARY_H_ */
