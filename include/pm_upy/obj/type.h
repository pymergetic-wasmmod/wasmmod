/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_UPY_OBJ_TYPE_H_
#define PM_PM_UPY_OBJ_TYPE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include "pm_upy/obj/core.h"
int pm_upy_obj_is_true(pm_upy_obj_t o);
int pm_upy_obj_is_callable(pm_upy_obj_t o);

pm_upy_obj_t pm_upy_obj_new_bound_meth(pm_upy_obj_t meth, pm_upy_obj_t self);
pm_upy_obj_t pm_upy_obj_new_closure(pm_upy_obj_t fun, size_t n, pm_upy_obj_t *closed);
pm_upy_obj_t pm_upy_obj_new_gen_wrap(pm_upy_obj_t fun);
pm_upy_obj_t pm_upy_obj_new_cell(pm_upy_obj_t obj);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_UPY_OBJ_TYPE_H_ */
