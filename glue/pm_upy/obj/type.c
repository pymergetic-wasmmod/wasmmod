/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#include "pm_upy/obj/type.h"
#include "py/obj.h"

int pm_upy_obj_is_true(pm_upy_obj_t o) {
    return mp_obj_is_true((mp_obj_t)(uintptr_t)o);
}

int pm_upy_obj_is_callable(pm_upy_obj_t o) {
    return mp_obj_is_callable((mp_obj_t)(uintptr_t)o);
}
