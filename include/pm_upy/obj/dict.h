/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_UPY_OBJ_DICT_H_
#define PM_PM_UPY_OBJ_DICT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "pm_upy/obj/core.h"
pm_upy_obj_t pm_upy_obj_new_dict(void);
int pm_upy_dict_store(pm_upy_obj_t dict, pm_upy_obj_t key, pm_upy_obj_t val);
pm_upy_obj_t pm_upy_dict_get(pm_upy_obj_t dict, pm_upy_obj_t key);
int pm_upy_dict_delete(pm_upy_obj_t dict, pm_upy_obj_t key);
pm_upy_obj_t pm_upy_dict_copy(pm_upy_obj_t dict);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_UPY_OBJ_DICT_H_ */
