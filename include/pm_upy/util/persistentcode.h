/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_UPY_UTIL_PERSISTENTCODE_H_
#define PM_PM_UPY_UTIL_PERSISTENTCODE_H_

#ifdef __cplusplus
extern "C" {
#endif

int pm_upy_persistentcode_available(void);

#include "pm_upy/obj/core.h"
int pm_upy_persistentcode_save_fun(pm_upy_obj_t fun, void *writer);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_UPY_UTIL_PERSISTENTCODE_H_ */
