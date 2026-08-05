/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_UPY_OBJ_CALL_H_
#define PM_PM_UPY_OBJ_CALL_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "pm_upy/obj/core.h"
pm_upy_obj_t pm_upy_call_function_0(pm_upy_obj_t fun);
pm_upy_obj_t pm_upy_call_function_1(pm_upy_obj_t fun, pm_upy_obj_t arg);
uint32_t pm_upy_fn_resolve(const char *dotted);
int pm_upy_fn_call_i32(uint32_t fn_h, int32_t a, int32_t b, int32_t *out);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_UPY_OBJ_CALL_H_ */
