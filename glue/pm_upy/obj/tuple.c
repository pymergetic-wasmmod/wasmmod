/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#include "pm_upy/obj/tuple.h"
#include "py/obj.h"

pm_upy_obj_t pm_upy_obj_new_tuple(size_t n) {
    return (pm_upy_obj_t)(uintptr_t)mp_obj_new_tuple(n, NULL);
}
