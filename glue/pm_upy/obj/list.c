/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#include "pm_upy/obj/list.h"
#include "pm_common.h"
#include "py/objlist.h"
#include "py/runtime.h"

pm_upy_obj_t pm_upy_obj_new_list(size_t n) {
    return (pm_upy_obj_t)(uintptr_t)mp_obj_new_list(n, NULL);
}

int pm_upy_list_append(pm_upy_obj_t list, pm_upy_obj_t item) {
    mp_obj_list_append((mp_obj_t)(uintptr_t)list, (mp_obj_t)(uintptr_t)item);
    return PM_OK;
}

int pm_upy_list_remove(pm_upy_obj_t list, pm_upy_obj_t item) {
    mp_obj_t args[2] = {
        (mp_obj_t)(uintptr_t)list,
        (mp_obj_t)(uintptr_t)item,
    };
    /* list.remove via type method lookup would raise; use binary op delete by value. */
    mp_obj_t meth = mp_load_attr(args[0], MP_QSTR_remove);
    mp_call_function_1(meth, args[1]);
    return PM_OK;
}

int pm_upy_list_sort(pm_upy_obj_t list) {
    mp_obj_t meth = mp_load_attr((mp_obj_t)(uintptr_t)list, MP_QSTR_sort);
    mp_call_function_0(meth);
    return PM_OK;
}
