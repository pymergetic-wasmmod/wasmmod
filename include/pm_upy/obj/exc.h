/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_UPY_OBJ_EXC_H_
#define PM_PM_UPY_OBJ_EXC_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "pm_upy/obj/core.h"
void pm_upy_raise_msg(const char *type_name, const char *msg);
void pm_upy_raise_feature(const char *api_name);

void pm_upy_raise_OSError(int errno_val);
pm_upy_obj_t pm_upy_obj_new_exception(const char *type_name, const char *msg);
void pm_upy_obj_print_exception(pm_upy_obj_t exc);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_UPY_OBJ_EXC_H_ */
