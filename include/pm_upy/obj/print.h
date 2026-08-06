/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_UPY_OBJ_PRINT_H_
#define PM_PM_UPY_OBJ_PRINT_H_

#ifdef __cplusplus
extern "C" {
#endif

void pm_upy_printf(const char *fmt, ...);

#include "pm_upy/obj/core.h"
void pm_upy_obj_print(pm_upy_obj_t o);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_UPY_OBJ_PRINT_H_ */
