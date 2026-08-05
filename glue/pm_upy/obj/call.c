/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#include "pm_upy/obj/call.h"
#include "pm_common.h"
#include "py/runtime.h"

pm_upy_obj_t pm_upy_call_function_0(pm_upy_obj_t fun) {
    return (pm_upy_obj_t)(uintptr_t)mp_call_function_0((mp_obj_t)(uintptr_t)fun);
}

pm_upy_obj_t pm_upy_call_function_1(pm_upy_obj_t fun, pm_upy_obj_t arg) {
    return (pm_upy_obj_t)(uintptr_t)mp_call_function_1(
        (mp_obj_t)(uintptr_t)fun, (mp_obj_t)(uintptr_t)arg);
}

uint32_t pm_upy_fn_resolve(const char *dotted) {
    (void)dotted;
    return 0;
}

int pm_upy_fn_call_i32(uint32_t fn_h, int32_t a, int32_t b, int32_t *out) {
    (void)fn_h;
    (void)a;
    (void)b;
    (void)out;
    return PM_ERR_FEATURE;
}
