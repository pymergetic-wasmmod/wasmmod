/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#include "pm_upy/obj/core.h"
#include "py/obj.h"
#include "py/objstr.h"

pm_upy_obj_t pm_upy_obj_none(void) {
    return (pm_upy_obj_t)(uintptr_t)mp_const_none;
}

pm_upy_obj_t pm_upy_obj_new_int_from_ll(long long i) {
    return (pm_upy_obj_t)(uintptr_t)mp_obj_new_int_from_ll(i);
}

long long pm_upy_obj_get_ll(pm_upy_obj_t o) {
    return mp_obj_get_ll((mp_obj_t)(uintptr_t)o);
}

pm_upy_obj_t pm_upy_obj_new_str(const char *s, size_t len) {
    return (pm_upy_obj_t)(uintptr_t)mp_obj_new_str(s, len);
}

pm_upy_obj_t pm_upy_obj_new_bytes(const uint8_t *b, size_t len) {
    return (pm_upy_obj_t)(uintptr_t)mp_obj_new_bytes(b, len);
}
