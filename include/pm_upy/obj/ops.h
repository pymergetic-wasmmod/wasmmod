/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_UPY_OBJ_OPS_H_
#define PM_PM_UPY_OBJ_OPS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "pm_upy/obj/core.h"

int pm_upy_ops_available(void);
pm_upy_obj_t pm_upy_unary_op(int op, pm_upy_obj_t o);
pm_upy_obj_t pm_upy_binary_op(int op, pm_upy_obj_t lhs, pm_upy_obj_t rhs);
pm_upy_obj_t pm_upy_getiter(pm_upy_obj_t o);
pm_upy_obj_t pm_upy_iternext(pm_upy_obj_t o);
pm_upy_obj_t pm_upy_subscr(pm_upy_obj_t base, pm_upy_obj_t index, pm_upy_obj_t value);
size_t pm_upy_len(pm_upy_obj_t o);
int pm_upy_equal(pm_upy_obj_t a, pm_upy_obj_t b);
pm_upy_obj_t pm_upy_get_type(pm_upy_obj_t o);
int pm_upy_is_subclass(pm_upy_obj_t obj, pm_upy_obj_t classinfo);
pm_upy_obj_t pm_upy_load_global(const char *name);
int pm_upy_store_global(const char *name, pm_upy_obj_t val);
pm_upy_obj_t pm_upy_load_name(const char *name);
int pm_upy_store_name(const char *name, pm_upy_obj_t val);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_UPY_OBJ_OPS_H_ */
