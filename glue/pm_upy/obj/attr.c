/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#include "pm_upy/obj/attr.h"
#include "pm_common.h"
#include "py/runtime.h"

pm_upy_obj_t pm_upy_load_attr(pm_upy_obj_t obj, const char *attr) {
    return (pm_upy_obj_t)(uintptr_t)mp_load_attr((mp_obj_t)(uintptr_t)obj, qstr_from_str(attr));
}

int pm_upy_store_attr(pm_upy_obj_t obj, const char *attr, pm_upy_obj_t val) {
    mp_store_attr((mp_obj_t)(uintptr_t)obj, qstr_from_str(attr), (mp_obj_t)(uintptr_t)val);
    return PM_OK;
}
