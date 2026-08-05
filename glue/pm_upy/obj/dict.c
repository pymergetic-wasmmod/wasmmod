/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#include "pm_upy/obj/dict.h"
#include "py/obj.h"

pm_upy_obj_t pm_upy_obj_new_dict(void) {
    return (pm_upy_obj_t)(uintptr_t)mp_obj_new_dict(0);
}

int pm_upy_dict_store(pm_upy_obj_t dict, pm_upy_obj_t key, pm_upy_obj_t val) {
    mp_obj_dict_store((mp_obj_t)(uintptr_t)dict, (mp_obj_t)(uintptr_t)key, (mp_obj_t)(uintptr_t)val);
    return 0;
}
