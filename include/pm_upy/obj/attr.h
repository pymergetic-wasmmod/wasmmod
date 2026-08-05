/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_UPY_OBJ_ATTR_H_
#define PM_PM_UPY_OBJ_ATTR_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "pm_upy/obj/core.h"
pm_upy_obj_t pm_upy_load_attr(pm_upy_obj_t obj, const char *attr);
int pm_upy_store_attr(pm_upy_obj_t obj, const char *attr, pm_upy_obj_t val);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_UPY_OBJ_ATTR_H_ */
