/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_UPY_OBJ_LIST_H_
#define PM_PM_UPY_OBJ_LIST_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "pm_upy/obj/core.h"
pm_upy_obj_t pm_upy_obj_new_list(size_t n);
int pm_upy_list_append(pm_upy_obj_t list, pm_upy_obj_t item);
int pm_upy_list_remove(pm_upy_obj_t list, pm_upy_obj_t item);
int pm_upy_list_sort(pm_upy_obj_t list);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_UPY_OBJ_LIST_H_ */
